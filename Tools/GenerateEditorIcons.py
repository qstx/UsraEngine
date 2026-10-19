"""Generate the Ursa-owned Editor icon atlas from vector primitives (Pillow required)."""
from pathlib import Path
import argparse
import hashlib
import io
import json
import math
import uuid
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "Assets/Icons/Editor"
CELL, COLUMNS, PADDING, SCALE = 48, 8, 6, 4
# Geometry uses a 24-unit viewbox. All artwork is original Ursa UI artwork.
def line(*points): return ("line", points)
def poly(*points): return ("polygon", points)
def box(x, y, w, h): return ("rect", (x, y, w, h))
def circle(x, y, r): return ("circle", (x, y, r))
def dot(x, y, r=1): return ("dot", (x, y, r))

cube = [line((12, 2), (21, 7), (21, 17), (12, 22), (3, 17), (3, 7), (12, 2)),
        line((3, 7), (12, 12), (21, 7)), line((12, 12), (12, 22))]
file = [line((5, 2), (14, 2), (20, 8), (20, 22), (5, 22), (5, 2)), line((14, 2), (14, 8), (20, 8))]
icons = {
    "Play": [poly((7, 3), (21, 12), (7, 21))],
    "Stop": [box(5, 5, 14, 14)],
    "Pause": [box(5, 4, 4, 16), box(15, 4, 4, 16)],
    "Step": [poly((4, 4), (15, 12), (4, 20)), box(18, 4, 3, 16)],
    "Move": [line((12, 2), (12, 22)), line((2, 12), (22, 12)), line((8, 6), (12, 2), (16, 6)), line((8, 18), (12, 22), (16, 18)), line((6, 8), (2, 12), (6, 16)), line((18, 8), (22, 12), (18, 16))],
    "Rotate": [line((20, 9), (18, 5), (13, 3), (7, 4), (3, 9), (4, 16), (9, 21), (16, 20), (20, 16)), line((20, 3), (20, 9), (14, 9))],
    "Scale": [box(3, 13, 8, 8), line((13, 11), (21, 3)), line((14, 3), (21, 3), (21, 10))],
    "Fit": [line((9, 3), (3, 3), (3, 9)), line((15, 3), (21, 3), (21, 9)), line((3, 15), (3, 21), (9, 21)), line((15, 21), (21, 21), (21, 15)), box(8, 8, 8, 8)],
    "Folder": [line((2, 6), (2, 21), (22, 21), (22, 8), (11, 8), (8, 4), (2, 4), (2, 6)), line((2, 11), (22, 11))],
    "File": file,
    "Scene": cube + [line((6, 16), (12, 19), (18, 16))],
    "Prefab": cube + [box(9, 8, 6, 6)],
    "Material": [circle(12, 12, 9), line((5, 6), (19, 18)), line((3, 12), (21, 12))],
    "Texture": [box(3, 3, 18, 18), circle(8, 8, 2), line((3, 18), (10, 12), (14, 16), (18, 11), (21, 14))],
    "Shader": [line((8, 5), (2, 12), (8, 19)), line((16, 5), (22, 12), (16, 19)), line((14, 3), (10, 21))],
    "Mesh": cube + [line((3, 7), (12, 22), (21, 7)), line((3, 17), (21, 17))],
    "Model": cube + [line((8, 8), (12, 5), (16, 8))],
    "Script": file + [line((9, 11), (7, 14), (9, 17)), line((15, 11), (17, 14), (15, 17))],
    "ScriptableObject": file + [circle(12, 14, 4), dot(12, 14)],
    "GameObject": cube,
    "Camera": [box(2, 7, 14, 12), poly((16, 11), (22, 7), (22, 19), (16, 15)), circle(8, 12, 3)],
    "Light": [circle(12, 12, 4)] + [line((12+7*math.cos(a), 12+7*math.sin(a)), (12+10*math.cos(a), 12+10*math.sin(a))) for a in [i*math.pi/4 for i in range(8)]],
    "PointLight": [circle(12, 9, 6), line((8, 14), (9, 19), (15, 19), (16, 14)), line((10, 22), (14, 22))],
    "Renderer": [box(2, 3, 20, 14), line((8, 22), (16, 22)), line((12, 17), (12, 22)), line((6, 13), (11, 7), (17, 13))],
    "SkinnedMesh": [circle(12, 4, 2), line((12, 6), (12, 14), (6, 21)), line((12, 14), (18, 21)), line((4, 9), (12, 11), (20, 9))],
    "Component": [box(4, 4, 16, 16), box(9, 9, 6, 6), line((9, 1), (9, 4)), line((15, 1), (15, 4)), line((9, 20), (9, 23)), line((15, 20), (15, 23))],
    "Add": [line((12, 4), (12, 20)), line((4, 12), (20, 12))],
    "Remove": [line((4, 12), (20, 12))],
    "More": [dot(5, 12, 1.7), dot(12, 12, 1.7), dot(19, 12, 1.7)],
    "Refresh": [line((20, 9), (18, 4), (9, 3), (3, 8)), line((20, 3), (20, 9), (14, 9)), line((4, 15), (6, 20), (15, 21), (21, 16)), line((4, 21), (4, 15), (10, 15))],
    "Search": [circle(10, 10, 7), line((15, 15), (22, 22))],
    "Rename": [line((4, 16), (16, 4), (20, 8), (8, 20), (3, 21), (4, 16)), line((14, 6), (18, 10))],
    "Save": [line((3, 3), (18, 3), (21, 6), (21, 21), (3, 21), (3, 3)), box(7, 3, 9, 6), box(7, 14, 10, 7)],
    "Delete": [line((4, 6), (20, 6)), line((6, 6), (7, 21), (17, 21), (18, 6)), line((9, 3), (15, 3)), line((10, 10), (10, 17)), line((14, 10), (14, 17))],
    "Close": [line((5, 5), (19, 19)), line((19, 5), (5, 19))],
    "Check": [line((3, 12), (9, 18), (21, 5))],
    "Revert": [line((9, 4), (3, 10), (9, 16)), line((3, 10), (15, 10), (20, 14), (19, 19), (13, 21))],
    "Up": [line((5, 15), (12, 8), (19, 15))],
    "Down": [line((5, 8), (12, 15), (19, 8))],
    "Locate": [circle(12, 12, 7), circle(12, 12, 2), line((12, 1), (12, 5)), line((12, 19), (12, 23)), line((1, 12), (5, 12)), line((19, 12), (23, 12))],
    "Trace": [dot(4, 12), dot(12, 12), dot(20, 12), line((5, 12), (10, 12)), line((14, 12), (18, 12))],
    "Debug": [box(7, 6, 10, 14), line((9, 3), (11, 6)), line((15, 3), (13, 6)), line((3, 9), (7, 11)), line((17, 11), (21, 9)), line((3, 17), (7, 15)), line((17, 15), (21, 17)), line((12, 10), (12, 19))],
    "Info": [circle(12, 12, 10), dot(12, 7), line((12, 11), (12, 18))],
    "Warning": [line((12, 2), (23, 21), (1, 21), (12, 2)), line((12, 8), (12, 14)), dot(12, 18)],
    "Error": [circle(12, 12, 10), line((8, 8), (16, 16)), line((16, 8), (8, 16))],
    "Import": [line((12, 2), (12, 15)), line((7, 10), (12, 15), (17, 10)), line((3, 15), (3, 22), (21, 22), (21, 15))],
    "Build": [line((3, 5), (8, 2), (13, 7), (10, 10), (21, 21), (18, 23), (7, 12), (4, 14), (1, 10), (3, 5))],
    "Settings": [circle(12, 12, 4), circle(12, 12, 9), line((12, 0), (12, 4)), line((12, 20), (12, 24)), line((0, 12), (4, 12)), line((20, 12), (24, 12))],
    "Project": [box(2, 4, 20, 17), line((2, 9), (22, 9)), line((8, 9), (8, 21))],
    "Hierarchy": [box(9, 1, 6, 5), line((12, 6), (12, 12)), line((4, 17), (4, 12), (20, 12), (20, 17)), box(1, 17, 6, 5), box(17, 17, 6, 5)],
    "Inspector": [box(3, 2, 18, 20), line((7, 7), (17, 7)), line((7, 12), (17, 12)), line((7, 17), (14, 17))],
    "Console": [box(2, 3, 20, 18), line((6, 8), (10, 12), (6, 16)), line((13, 16), (18, 16))],
    "Link": [line((10, 7), (14, 3), (19, 3), (22, 6), (22, 10), (17, 15)), line((14, 17), (10, 21), (5, 21), (2, 18), (2, 14), (7, 9)), line((8, 16), (16, 8))],
    "Copy": [box(7, 7, 15, 15), line((3, 17), (2, 17), (2, 2), (17, 2), (17, 3))],
    "Pin": [line((8, 2), (18, 2), (16, 10), (21, 15), (3, 15), (8, 10), (8, 2)), line((12, 15), (12, 23))],
}

def generate():
    atlas = Image.new("RGBA", (COLUMNS*CELL*SCALE, math.ceil(len(icons)/COLUMNS)*CELL*SCALE))
    manifest = {"version": 1, "cell": CELL, "columns": COLUMNS, "width": atlas.width//SCALE, "height": atlas.height//SCALE, "icons": {}}
    svgs = {}
    for i, (name, shapes) in enumerate(icons.items()):
        col, row = i % COLUMNS, i // COLUMNS
        tile = Image.new("RGBA", (CELL*SCALE, CELL*SCALE))
        d = ImageDraw.Draw(tile)
        factor = (CELL-2*PADDING)/24*SCALE
        point = lambda p: tuple(PADDING*SCALE+v*factor for v in p)
        parts = []
        for kind, coords in shapes:
            if kind in ("line", "polygon"):
                pts = [point(p) for p in coords]
                text = " ".join(f"{x:g},{y:g}" for x, y in coords)
                if kind == "line":
                    d.line(pts, fill="white", width=round(1.6*factor), joint="curve")
                    parts.append(f'<polyline points="{text}" fill="none" stroke="white" stroke-width="1.6" stroke-linejoin="round"/>')
                else:
                    d.polygon(pts, fill="white")
                    parts.append(f'<polygon points="{text}" fill="white"/>')
            elif kind == "rect":
                x,y,w,h = coords
                # Play controls are filled; small indicators are outlined elsewhere.
                fill = name in ("Pause", "Stop", "Step")
                d.rectangle([*point((x,y)), *point((x+w,y+h))], fill="white" if fill else None, outline="white", width=round(1.6*factor))
                parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{"white" if fill else "none"}" stroke="white" stroke-width="1.6"/>')
            else:
                x,y,r = coords
                d.ellipse([*point((x-r,y-r)), *point((x+r,y+r))], fill="white" if kind=="dot" else None, outline="white", width=round(1.6*factor))
                parts.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{"white" if kind=="dot" else "none"}" stroke="white" stroke-width="1.6"/>')
        atlas.alpha_composite(tile, (col*CELL*SCALE, row*CELL*SCALE))
        manifest["icons"][name] = {"index": i, "uv0": [col*CELL/manifest["width"], row*CELL/manifest["height"]], "uv1": [(col+1)*CELL/manifest["width"], (row+1)*CELL/manifest["height"]]}
        svgs[f"Sources/{name}.svg"] = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">'+"".join(parts)+"</svg>\n").encode()
    stream = io.BytesIO()
    atlas.resize((manifest["width"], manifest["height"]), Image.Resampling.LANCZOS).save(stream, format="PNG")
    return {"EditorIcons.png": stream.getvalue(), "EditorIcons.json": (json.dumps(manifest, indent=2)+"\n").encode(), **svgs}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    artifacts = generate()
    for relative, content in artifacts.items():
        path = OUT / relative
        if args.check:
            if not path.exists() or path.read_bytes() != content:
                raise SystemExit(f"Generated file differs: {path}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
    # Stable metadata is generated only for new resources; existing GUIDs are never overwritten.
    if not args.check:
        for path in [ROOT/"Assets/Icons", OUT, OUT/"Sources", *[p for p in OUT.rglob("*") if p.is_file() and p.suffix != ".meta"]]:
            meta = path.with_name(path.name+".meta")
            if not meta.exists():
                meta.write_text(json.dumps({"version": 1, "guid": str(uuid.uuid5(uuid.NAMESPACE_URL, "ursa:"+path.relative_to(ROOT/"Assets").as_posix())), "folder": path.is_dir(), "importer": "", "importerVersion": 0, "settings": {}}, indent=2)+"\n", encoding="utf-8")
    print(f'{"Verified" if args.check else "Generated"} {len(icons)} icons; SHA256 {hashlib.sha256(artifacts["EditorIcons.png"]).hexdigest()}')

if __name__ == "__main__":
    main()
