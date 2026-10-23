"""Capture one isolated Player run with the pinned Tracy bindings and export complete frame samples."""

import argparse
import bisect
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import shutil
import socket
import statistics
import subprocess
import sys
import time


CPU_REGIONS = (
    "Engine.Frame", "Engine.GameFrame", "Engine.RenderPreparation",
    "Rendering.FrameSlotWait", "Rendering.SwapchainAcquire", "Rendering.SwapchainPresent",
    "SceneDrawEncoder.CacheMaintenance",
    "SceneDrawEncoder.Record", "SceneDrawEncoder.Record.BuildPasses",
    "SceneDrawEncoder.Record.BuildBindGroups", "SceneDrawEncoder.Record.EncodeCommands",
)


def summarize(values):
    if not values:
        return {"count": 0, "medianMs": None, "p95Ms": None, "totalMs": 0}
    ordered = sorted(values)
    position = (len(ordered) - 1) * 0.95
    low = math.floor(position)
    high = math.ceil(position)
    p95 = ordered[low] + (ordered[high] - ordered[low]) * (position - low)
    return {"count": len(values), "medianMs": statistics.median(values) / 1e6,
            "p95Ms": p95 / 1e6, "totalMs": sum(values) / 1e6}


def file_hash(path):
    hasher = hashlib.sha256()
    with open(path, "rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            hasher.update(block)
    return hasher.hexdigest()


def export(worker, args):
    limit = 2_000_000
    # The pinned bindings return (start, duration), whereas frame boundaries return (start, end).
    frames = sorted((start, start + duration)
                    for start, duration in worker.get_zone_occurrences("Engine.Frame", limit) if duration > 0)
    required = args.warmup + args.samples + 1
    if len(frames) < required:
        raise RuntimeError(f"Only {len(frames)} complete Engine.Frame events; need at least {required}")
    selected = frames[args.warmup:args.warmup + args.samples]
    starts = [start for start, _ in selected]
    window_end = frames[args.warmup + args.samples][0]
    intervals = [frames[index + 1][0] - frames[index][0]
                 for index in range(args.warmup, args.warmup + args.samples)]
    output = {"completeEngineFrames": len(frames), "frameMarks": worker.get_frame_count(),
              "warmupFrames": args.warmup, "sampleFrames": args.samples,
              "windowStartNs": starts[0], "windowEndNs": window_end,
              "frameInterval": summarize(intervals), "frameIntervalsNs": intervals,
              "engineCpuScope": summarize([end - start for start, end in selected]),
              "cpuRegions": {}, "gpuRegions": {}, "plots": [], "memoryPools": [],
              "platform": platform.platform(), "cpuTimerResolutionNs": worker.get_resolution()}
    try:
        output["hostInfo"] = worker.get_host_info()
    except UnicodeDecodeError:
        output["hostInfoEncodingNote"] = "Tracy host text uses the Windows ANSI locale and cannot be decoded by its UTF-8 binding. Platform, adapter log and external device metadata are recorded separately."
    for name in CPU_REGIONS:
        occurrences = worker.get_zone_occurrences(name, limit)
        if len(occurrences) >= limit:
            raise RuntimeError(f"CPU occurrence query reached its limit: {name}")
        per_frame = [0] * args.samples
        accepted = []
        for start, duration in occurrences:
            if starts[0] <= start < window_end and duration >= 0:
                per_frame[bisect.bisect_right(starts, start) - 1] += duration
                accepted.append([start, start + duration])
        if accepted:
            output["cpuRegions"][name] = {"perFrame": summarize(per_frame),
                                           "perFrameNs": per_frame, "occurrencesNs": accepted}
    for name in ("Vulkan.Draw", "Vulkan.DrawIndexed", "Vulkan.Dispatch"):
        occurrences = worker.get_gpu_zone_occurrences(name, limit)
        if len(occurrences) >= limit:
            raise RuntimeError(f"GPU occurrence query reached its limit: {name}")
        accepted = [(start, start + duration) for start, duration in occurrences
                    if duration > 0 and starts[0] <= start and start + duration <= window_end]
        if accepted:
            output["gpuRegions"][name] = {"perOperation": summarize([end - start for start, end in accepted]),
                                           "occurrencesNs": accepted}
    for plot in worker.get_plots():
        fields = {}
        for name in ("name", "min", "max", "count"):
            if hasattr(plot, name):
                fields[name] = getattr(plot, name)
        output["plots"].append(fields)
    for pool in worker.get_memory_pools():
        output["memoryPools"].append({name: getattr(pool, name)
                                     for name in ("name", "high", "low", "usage", "alloc_count")})
    output["gpuCoverage"] = "Complete draw/dispatch operations within the CPU sample time window; excludes barriers, copies, presentation and uninstrumented GPU work. No claim of whole-frame GPU time."
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--player", type=Path, required=True)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--scene", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=1320)
    parser.add_argument("--warmup", type=int, default=120)
    parser.add_argument("--samples", type=int, default=900)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--bindings", type=Path, default=Path(__file__).resolve().parent /
                        "TracyMcp/Release/WindowsX64/0.14.1")
    parser.add_argument("--source-revision", default="")
    args = parser.parse_args()
    if args.warmup < 0 or args.samples < 1 or args.frames <= args.warmup + args.samples:
        parser.error("Require nonnegative warmup, positive samples and an extra complete frame")
    args.output.mkdir(parents=True, exist_ok=False)
    sys.path.insert(0, str(args.bindings.resolve()))
    import TracyServerBindings as tracy

    # Refuse to connect to another running test or a user-owned Tracy client.
    with socket.socket() as probe:
        if probe.connect_ex(("127.0.0.1", 8086)) == 0:
            raise RuntimeError("Tracy port 8086 is already occupied; finish the other client first")
    worker = tracy.Worker("127.0.0.1", 8086, 2 * 1024 * 1024 * 1024)
    process = None
    command = [str(args.player.resolve()), "--project", str(args.project.resolve()),
               "--scene", args.scene, "--hidden", "--frames", str(args.frames)]
    environment = os.environ.copy()
    environment["URSA_USER_DATA_ROOT"] = str((args.output / "UserData").resolve())
    try:
        with (args.output / "Player.log").open("wb") as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, env=environment)
            deadline = time.monotonic() + args.timeout
            connected = False
            while process.poll() is None:
                if worker.is_connected():
                    connected = True
                    if worker.get_pid() != process.pid:
                        raise RuntimeError("Tracy connected to a different process")
                if time.monotonic() >= deadline:
                    raise TimeoutError("Player capture timed out")
                time.sleep(0.05)
            if process.returncode or not connected:
                raise RuntimeError(f"Player exited with {process.returncode}; connected={connected}; see Player.log")
        deadline = time.monotonic() + 30
        while worker.is_connected() or not worker.is_background_done():
            if time.monotonic() > deadline:
                raise TimeoutError("Tracy did not finish draining the capture")
            time.sleep(0.05)
        trace = args.output / "Capture.tracy"
        tracy.save_worker(worker, str(trace.resolve()), 3, 4, False)
        report = export(worker, args)
        report.update({"command": command, "playerSha256": file_hash(args.player),
                       "traceSha256": file_hash(trace), "exitCode": process.returncode,
                       "capturePid": process.pid, "vsync": "FIFO; Player default enabled",
                       "resolution": [1024, 720], "screenshotsDuringCapture": False})
        report["sourceRevision"] = args.source_revision
        project_assets = args.project / "Assets"
        report["projectAssetFiles"] = {}
        candidates = [project_assets / args.scene, project_assets / (args.scene + ".meta")]
        scripts = project_assets / "Scripts"
        if scripts.is_dir():
            candidates.extend(sorted(scripts.rglob("*.cs")))
        for source in candidates:
            if source.is_file():
                relative = str(source.relative_to(args.project)).replace("\\", "/")
                report["projectAssetFiles"][relative] = file_hash(source)
        report["adapterLog"] = [line for line in (args.output / "Player.log").read_text(
            encoding="utf-8", errors="replace").splitlines() if "Vulkan adapter:" in line]
        report["deviceMetadataAfterExit"] = {}
        for tool, options in (("powercfg", ["/getactivescheme"]),
                              ("nvidia-smi", ["--query-gpu=name,driver_version,pstate,temperature.gpu,clocks.gr,clocks.mem,display_active", "--format=csv"])):
            executable = shutil.which(tool)
            if executable:
                metadata = subprocess.run([executable, *options], capture_output=True, timeout=15)
                report["deviceMetadataAfterExit"][tool] = metadata.stdout.decode("utf-8", errors="replace")
        cache = args.player.resolve().parent.parent / "CMakeCache.txt"
        if cache.exists():
            report["buildFlags"] = [line for line in cache.read_text(encoding="utf-8").splitlines()
                                    if line.startswith(("URSA_ENABLE_", "URSA_TRACY_", "CMAKE_CXX_COMPILER:"))]
        (args.output / "Samples.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps({"output": str(args.output), "frames": report["sampleFrames"],
                          "frameInterval": report["frameInterval"]}), flush=True)
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            process.wait(timeout=15)
        worker.shutdown()


if __name__ == "__main__":
    main()
