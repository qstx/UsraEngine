using System.Collections;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text.Json;

internal static class Program
{
    [DllImport("ole32.dll")] private static extern int GetRunningObjectTable(int reserved, out IRunningObjectTable table);
    [DllImport("ole32.dll")] private static extern int CreateBindCtx(int reserved, out IBindCtx context);
    [DllImport("ole32.dll")] private static extern int CoRegisterMessageFilter(IOleMessageFilter? filter, out IOleMessageFilter? previous);

    [ComImport, Guid("00000016-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IOleMessageFilter
    {
        [PreserveSig] int HandleInComingCall(int type, IntPtr caller, int elapsed, IntPtr info);
        [PreserveSig] int RetryRejectedCall(IntPtr callee, int elapsed, int rejectType);
        [PreserveSig] int MessagePending(IntPtr callee, int elapsed, int pendingType);
    }

    private sealed class AutomationMessageFilter : IOleMessageFilter
    {
        public int HandleInComingCall(int type, IntPtr caller, int elapsed, IntPtr info) => 0;
        public int RetryRejectedCall(IntPtr callee, int elapsed, int rejectType) => elapsed < 10000 && rejectType is 1 or 2 ? 100 : -1;
        public int MessagePending(IntPtr callee, int elapsed, int pendingType) => 2;
    }

    private static EnvDTE.DTE Dte(string solution)
    {
        Marshal.ThrowExceptionForHR(GetRunningObjectTable(0, out var table));
        Marshal.ThrowExceptionForHR(CreateBindCtx(0, out var context));
        table.EnumRunning(out var enumerator);
        var monikers = new IMoniker[1];
        while (enumerator.Next(1, monikers, IntPtr.Zero) == 0)
        {
            monikers[0].GetDisplayName(context, null, out string name);
            if (!name.StartsWith("!VisualStudio.DTE.")) continue;
            table.GetObject(monikers[0], out object instance);
            var dte = (EnvDTE.DTE)instance;
            try { if (string.Equals((string)dte.Solution.FullName, solution, StringComparison.OrdinalIgnoreCase)) return dte; }
            catch (COMException) { }
        }
        throw new InvalidOperationException("No Visual Studio instance for " + solution);
    }

    private static T Retry<T>(Func<T> action)
    {
        DateTime deadline = DateTime.UtcNow.AddSeconds(8);
        while (true)
        {
            try { return action(); }
            catch (COMException error) when ((error.HResult == unchecked((int)0x80010001) || error.HResult == unchecked((int)0x8001010A)) && DateTime.UtcNow < deadline) { Thread.Sleep(100); }
        }
    }

    private static void Move(EnvDTE.TextSelection selection, string text, int offset)
    {
        string prefix = text[..offset];
        int line = prefix.Count(character => character == '\n') + 1;
        int column = offset - prefix.LastIndexOf('\n');
        selection.MoveToLineAndOffset(line, column, false);
    }

    private static EnvDTE.Document OpenProjectFile(EnvDTE.DTE dte, string path)
    {
        string fullPath = Path.GetFullPath(path);
        EnvDTE.ProjectItem? item = dte.Solution.FindProjectItem(fullPath);
        if (item?.ContainingProject?.FullName?.EndsWith(".csproj", StringComparison.OrdinalIgnoreCase) != true)
        {
            EnvDTE.ProjectItem? FindFiles(EnvDTE.ProjectItems items)
            {
                foreach (EnvDTE.ProjectItem candidate in items)
                {
                    for (short index = 1; index <= candidate.FileCount; ++index)
                        if (string.Equals(Path.GetFullPath(candidate.FileNames[index]), fullPath, StringComparison.OrdinalIgnoreCase)) return candidate;
                    if (candidate.ProjectItems is { } children && FindFiles(children) is { } found) return found;
                }
                return null;
            }
            EnvDTE.ProjectItem? FindProject(EnvDTE.Project project)
            {
                if (project.FullName.EndsWith(".csproj", StringComparison.OrdinalIgnoreCase)) return FindFiles(project.ProjectItems);
                if (!string.IsNullOrEmpty(project.FullName)) return null;
                foreach (EnvDTE.ProjectItem child in project.ProjectItems)
                    if (child.SubProject is { } sub && FindProject(sub) is { } found) return found;
                return null;
            }
            item = null;
            foreach (EnvDTE.Project project in dte.Solution.Projects)
                if (FindProject(project) is { } found) { item = found; break; }
        }
        string? projectPath = item?.ContainingProject?.FullName;
        if (item == null || string.IsNullOrEmpty(projectPath) || !projectPath.EndsWith(".csproj", StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("C# source is not loaded in a solution project: " + fullPath);
        EnvDTE.Window window = item.Open(EnvDTE.Constants.vsViewKindCode);
        window.Activate();
        EnvDTE.Document document = window.Document;
        if (!string.Equals(document.ProjectItem?.ContainingProject?.FullName, projectPath, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("C# source opened outside its solution project: " + fullPath);
        return document;
    }

    private static object VerifyLanguage(EnvDTE.DTE dte, string path, string? definition, string? navigationSymbol = null,
        string expression = "Transform.LocalPosition", string prefix = "LocalRotat", string completedMember = "LocalRotation")
    {
        if (dte.Debugger.CurrentMode != EnvDTE.dbgDebugMode.dbgDesignMode || dte.UndoContext.IsOpen)
            throw new InvalidOperationException("IDE language verification requires an idle solution.");
        EnvDTE.Document document = OpenProjectFile(dte, path);
        var text = (EnvDTE.TextDocument)document.Object("TextDocument");
        var selection = (EnvDTE.TextSelection)document.Selection;
        string original = text.StartPoint.CreateEditPoint().GetText(text.EndPoint);
        if (!document.Saved) throw new InvalidOperationException("Refusing to change an unsaved document.");
        string disk = File.ReadAllText(path);
        if (disk.Replace("\r\n", "\n") != original.Replace("\r\n", "\n")) throw new InvalidOperationException("IDE buffer does not match the source on disk.");
        object result;
        dte.UndoContext.Open("Ursa IDE acceptance", false);
        try
        {
            if (definition == null)
            {
                string member = expression[(expression.LastIndexOf('.') + 1)..];
                int expressionOffset = original.IndexOf(expression, StringComparison.Ordinal);
                if (expressionOffset < 0) throw new InvalidOperationException("Completion fixture not found.");
                int offset = expressionOffset + expression.Length - member.Length;
                Move(selection, original, offset);
                selection.MoveToLineAndOffset(selection.ActivePoint.Line, selection.ActivePoint.LineCharOffset + member.Length, true);
                selection.Text = prefix;
                Thread.Sleep(500);
                dte.ExecuteCommand("Edit.CompleteWord");
                DateTime deadline = DateTime.UtcNow.AddSeconds(12);
                bool completed = false;
                do
                {
                    string current = Retry(() => text.StartPoint.CreateEditPoint().GetText(text.EndPoint));
                    completed = current.Length >= offset + completedMember.Length && current.Substring(offset, completedMember.Length) == completedMember;
                    if (completed) break;
                    Thread.Sleep(150);
                } while (DateTime.UtcNow < deadline);
                if (!completed) throw new InvalidOperationException("IDE did not complete member " + completedMember +
                    "; project=" + document.ProjectItem?.ContainingProject?.Name + "; context=" +
                    text.StartPoint.CreateEditPoint().GetText(text.EndPoint).Substring(Math.Max(0, offset - 30), Math.Min(85, original.Length - Math.Max(0, offset - 30))));
                result = new { Action = "Completion", File = path, Project = document.ProjectItem.ContainingProject.FullName, Typed = prefix, Completed = completedMember };
            }
            else
            {
                string symbol = navigationSymbol ?? "MoveWithInputBehaviour";
                string extra = "\ninternal static class UrsaIdeNavigationProbe { internal static System.Type Target => typeof(" + symbol + "); }\n";
                selection.EndOfDocument(false);
                selection.Text = extra;
                string current = text.StartPoint.CreateEditPoint().GetText(text.EndPoint);
                int offset = current.LastIndexOf(symbol, StringComparison.Ordinal);
                Move(selection, current, offset + Math.Max(1, symbol.Length - 2));
                Thread.Sleep(800);
                dte.ExecuteCommand("Edit.GoToDefinition");
                DateTime deadline = DateTime.UtcNow.AddSeconds(12);
                bool navigated = false;
                string actual = "";
                do
                {
                    actual = Retry(() => dte.ActiveDocument.FullName);
                    navigated = string.Equals(Path.GetFullPath(actual), Path.GetFullPath(definition), StringComparison.OrdinalIgnoreCase);
                    if (navigated) break;
                    Thread.Sleep(150);
                } while (DateTime.UtcNow < deadline);
                if (!navigated) throw new InvalidOperationException("IDE did not navigate from the Editor project to Game source: " + actual);
                string targetProject = dte.ActiveDocument.ProjectItem?.ContainingProject?.FullName ?? "";
                if (!targetProject.EndsWith(".csproj", StringComparison.OrdinalIgnoreCase)) throw new InvalidOperationException("Definition opened outside its solution project: " + actual);
                result = new { Action = "ProjectReferenceNavigation", Source = path, SourceProject = document.ProjectItem.ContainingProject.FullName, Target = actual, TargetProject = targetProject };
            }
        }
        finally
        {
            Retry(() => { dte.UndoContext.Close(); return true; });
            Retry(() => { document.Activate(); dte.ExecuteCommand("Edit.Undo"); return true; });
            string restored = Retry(() => text.StartPoint.CreateEditPoint().GetText(text.EndPoint));
            if (restored != original || File.ReadAllText(path) != disk) throw new InvalidOperationException("Acceptance edit did not restore the original buffer/source.");
            if (!document.Saved) document.Saved = true;
        }
        return result;
    }

    [STAThread]
    private static int Main(string[] args)
    {
        var filter = new AutomationMessageFilter();
        IOleMessageFilter? previous = null;
        try
        {
            Marshal.ThrowExceptionForHR(CoRegisterMessageFilter(filter, out previous));
            var dte = Dte(Path.GetFullPath(args[1]));
            var debugger = (EnvDTE80.Debugger2)dte.Debugger;
            string action = args[0];
            if (action is "state" or "evaluate" or "step" or "continue" or "detach" or "break" or "thread-stacks" or "inspect-frame" or "stop")
            {
                string pidPath = Path.Combine(AppContext.BaseDirectory, "TargetPid.txt");
                int expected = File.Exists(pidPath) ? int.Parse(File.ReadAllText(pidPath)) : -1;
                bool found = false;
                foreach (EnvDTE.Process process in dte.Debugger.DebuggedProcesses) found |= process.ProcessID == expected;
                if (!found && dte.Debugger.CurrentMode != EnvDTE.dbgDebugMode.dbgDesignMode)
                    throw new InvalidOperationException("Not the acceptance debugger session.");
            }
            if (action == "info")
            {
                var engines = new List<string>();
                foreach (EnvDTE80.Transport transport in debugger.Transports)
                    foreach (EnvDTE80.Engine engine in transport.Engines) engines.Add((string)transport.Name + ":" + (string)engine.Name + ":" + (string)engine.ID);
                var projects = new List<string>();
                foreach (EnvDTE.Project project in dte.Solution.Projects) projects.Add((string)project.Name + ":" + (string)project.FullName);
                Console.WriteLine(JsonSerializer.Serialize(new { Version = (string)dte.Version, Projects = projects, Engines = engines, Mode = (int)dte.Debugger.CurrentMode }));
            }
            else if (action == "projects")
            {
                var projects = new List<object>();
                void Visit(EnvDTE.Project project)
                {
                    projects.Add(new { Name = project.Name, Path = project.FullName, Kind = project.Kind, Count = project.ProjectItems?.Count });
                    if (project.ProjectItems != null) foreach (EnvDTE.ProjectItem item in project.ProjectItems) if (item.SubProject is { } child) Visit(child);
                }
                foreach (EnvDTE.Project project in dte.Solution.Projects) Visit(project);
                Console.WriteLine(JsonSerializer.Serialize(projects));
            }
            else if (action == "errors")
            {
                var errors = new List<object>();
                var items = ((EnvDTE80.DTE2)dte).ToolWindows.ErrorList.ErrorItems;
                for (int index = 1; index <= Math.Min(items.Count, 30); ++index) { var item = items.Item(index); errors.Add(new { item.Description, item.FileName, item.Line }); }
                Console.WriteLine(JsonSerializer.Serialize(errors));
            }
            else if (action == "project-file")
            {
                var document = OpenProjectFile(dte, args[2]);
                Console.WriteLine(JsonSerializer.Serialize(new { File = document.FullName, Project = document.ProjectItem.ContainingProject.FullName }));
            }
            else if (action == "refresh-source")
            {
                string path = Path.GetFullPath(args[2]);
                foreach (EnvDTE.Document existing in dte.Documents)
                    if (string.Equals(existing.FullName, path, StringComparison.OrdinalIgnoreCase))
                    {
                        if (!existing.Saved) throw new InvalidOperationException("Refusing to reload an unsaved source buffer: " + path);
                        existing.Close(EnvDTE.vsSaveChanges.vsSaveChangesNo);
                        break;
                    }
                var document = OpenProjectFile(dte, path);
                var text = (EnvDTE.TextDocument)document.Object("TextDocument");
                if (text.StartPoint.CreateEditPoint().GetText(text.EndPoint).Replace("\r\n", "\n") != File.ReadAllText(path).Replace("\r\n", "\n"))
                    throw new InvalidOperationException("Reloaded source buffer does not match disk.");
                Console.WriteLine(JsonSerializer.Serialize(new { File = document.FullName, Project = document.ProjectItem.ContainingProject.FullName, Reloaded = true }));
            }
            else if (action == "close-fixture-documents")
            {
                if (dte.Debugger.CurrentMode != EnvDTE.dbgDebugMode.dbgDesignMode) throw new InvalidOperationException("Document cleanup requires an idle IDE.");
                string prefix = Path.Combine(Path.GetFullPath(args[2]), "IdeAcceptance", "DebugRun-");
                var targets = new List<EnvDTE.Document>();
                var skipped = new List<string>();
                foreach (EnvDTE.Document document in dte.Documents)
                    if (document.FullName.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                    { if (document.Saved) targets.Add(document); else skipped.Add(document.FullName); }
                var closed = targets.Select(document => document.FullName).ToArray();
                foreach (var document in targets) document.Close(EnvDTE.vsSaveChanges.vsSaveChangesNo);
                Console.WriteLine(JsonSerializer.Serialize(new { Closed = closed, UnsavedPreserved = skipped }));
            }
            else if (action == "complete") Console.WriteLine(JsonSerializer.Serialize(VerifyLanguage(dte, args[2], null)));
            else if (action == "navigate-definition") Console.WriteLine(JsonSerializer.Serialize(VerifyLanguage(dte, args[2], args[3])));
            else if (action == "navigate-symbol") Console.WriteLine(JsonSerializer.Serialize(VerifyLanguage(dte, args[2], args[3], args[4])));
            else if (action == "complete-member") Console.WriteLine(JsonSerializer.Serialize(VerifyLanguage(dte, args[2], null, null, args[3], args[4], args[5])));
            else if (action == "launch")
            {
                if (dte.Debugger.CurrentMode != EnvDTE.dbgDebugMode.dbgDesignMode)
                    throw new InvalidOperationException("Launch requires an idle acceptance IDE.");
                EnvDTE.Project? Find(EnvDTE.Project project)
                {
                    if (project.Name == args[2]) return project;
                    if (project.ProjectItems != null) foreach (EnvDTE.ProjectItem item in project.ProjectItems)
                        if (item.SubProject is { } child && Find(child) is { } found) return found;
                    return null;
                }
                EnvDTE.Project? target = null;
                foreach (EnvDTE.Project project in dte.Solution.Projects) if (Find(project) is { } found) { target = found; break; }
                if (target == null) throw new InvalidOperationException("Startup project not found: " + args[2]);
                if (args.Length > 3) dte.Solution.SolutionBuild.SolutionConfigurations.Item(args[3]).Activate();
                string configuration = dte.Solution.SolutionBuild.ActiveConfiguration.Name + "|x64";
                dynamic native = target.Object;
                dynamic settings = native.Configurations.Item(configuration).Rules.Item("WindowsLocalDebugger");
                string debuggerType = settings.GetEvaluatedPropertyValue("LocalDebuggerDebuggerType");
                object previousStartup = dte.Solution.SolutionBuild.StartupProjects;
                dte.Solution.SolutionBuild.StartupProjects = new[] { target.UniqueName };
                dte.ExecuteCommand("Debug.Start");
                DateTime deadline = DateTime.UtcNow.AddSeconds(30);
                while (dte.Debugger.DebuggedProcesses.Count == 0 && DateTime.UtcNow < deadline) Thread.Sleep(100);
                if (dte.Debugger.DebuggedProcesses.Count != 1) throw new InvalidOperationException("Expected one launched debugger process.");
                int pid = (int)dte.Debugger.DebuggedProcesses.Item(1).ProcessID;
                File.WriteAllText(Path.Combine(AppContext.BaseDirectory, "TargetPid.txt"), pid.ToString());
                Console.WriteLine(JsonSerializer.Serialize(new { Pid = pid, DebuggerType = debuggerType, PreviousStartup = previousStartup }));
            }
            else if (action == "startup")
            {
                if (dte.Debugger.CurrentMode != EnvDTE.dbgDebugMode.dbgDesignMode)
                    throw new InvalidOperationException("Restoring startup projects requires an idle IDE.");
                dte.Solution.SolutionBuild.StartupProjects = args.Skip(2).ToArray();
                Console.WriteLine("Restored startup projects");
            }
            else if (action == "stop")
            {
                if (dte.Debugger.DebuggedProcesses.Count > 1) throw new InvalidOperationException("Refusing to stop multiple debugger processes.");
                dte.Debugger.Stop(false);
                Console.WriteLine("Stopped acceptance debugger");
            }
            else if (action == "attach")
            {
                int pid = int.Parse(args[2]);
                if (dte.Debugger.DebuggedProcesses.Count != 0) throw new InvalidOperationException("IDE already has a debugger session; refusing to replace it.");
                foreach (EnvDTE.Process process in dte.Debugger.LocalProcesses)
                    if ((int)process.ProcessID == pid) { var engines = args[3].Split(';'); File.WriteAllText(Path.Combine(AppContext.BaseDirectory, "TargetPid.txt"), pid.ToString()); ((EnvDTE80.Process2)process).Attach2(engines.Length == 1 ? (object)engines[0] : engines); Console.WriteLine("Attached " + pid); return 0; }
                throw new InvalidOperationException("Target process was not listed");
            }
            else if (action == "breakpoint")
            {
                string file = Path.GetFullPath(args[2]);
                int line = int.Parse(args[3]);
                foreach (EnvDTE.Breakpoint point in dte.Debugger.Breakpoints)
                    if (string.Equals(point.File, file, StringComparison.OrdinalIgnoreCase) && point.FileLine == line)
                    { Console.WriteLine(JsonSerializer.Serialize(new { Created = false })); return 0; }
                dte.Debugger.Breakpoints.Add("", file, line, 1);
                Console.WriteLine(JsonSerializer.Serialize(new { Created = true }));
            }
            else if (action == "state")
            {
                int mode = (int)dte.Debugger.CurrentMode;
                string function = ""; string file = ""; string project = ""; int line = 0; var frames = new List<string>();
                if (mode == 2 && dte.Debugger.CurrentStackFrame is { } currentFrame)
                {
                    function = currentFrame.FunctionName;
                    foreach (EnvDTE.StackFrame frame in dte.Debugger.CurrentThread.StackFrames) { frames.Add(frame.FunctionName); if (frames.Count == 12) break; }
                    try { file = (string)dte.ActiveDocument.FullName; project = dte.ActiveDocument.ProjectItem?.ContainingProject?.FullName ?? ""; line = ((EnvDTE.TextSelection)dte.ActiveDocument.Selection).ActivePoint.Line; } catch { }
                }
                var processes = new List<int>();
                foreach (EnvDTE.Process process in dte.Debugger.DebuggedProcesses) processes.Add(process.ProcessID);
                Console.WriteLine(JsonSerializer.Serialize(new { Mode = mode, Function = function, File = file, Project = project, Line = line, Frames = frames, Processes = processes }));
            }
            else if (action == "evaluate")
            {
                var expression = Retry(() => dte.Debugger.GetExpression(args[2], false, 5000));
                bool valid = Retry(() => expression.IsValidValue);
                string value = Retry(() => expression.Value);
                Console.WriteLine(JsonSerializer.Serialize(new { Expression = args[2], Valid = valid, Value = value }));
            }
            else if (action == "inspect-frame")
            {
                foreach (EnvDTE.Thread thread in dte.Debugger.CurrentProgram.Threads)
                    foreach (EnvDTE.StackFrame frame in thread.StackFrames)
                        if (frame.FunctionName.Contains(args[2], StringComparison.Ordinal))
                        {
                            dte.Debugger.CurrentThread = thread;
                            dte.Debugger.CurrentStackFrame = frame;
                            var values = new List<object>();
                            foreach (string expression in args.Skip(3)) { var value = dte.Debugger.GetExpression(expression, true, 1000); values.Add(new { Expression = expression, Valid = (bool)value.IsValidValue, Value = (string)value.Value }); }
                            Console.WriteLine(JsonSerializer.Serialize(new { Function = frame.FunctionName, Values = values }));
                            return 0;
                        }
                throw new InvalidOperationException("Requested native frame not found.");
            }
            else if (action == "thread-stacks")
            {
                var threads = new List<object>();
                foreach (EnvDTE.Thread thread in dte.Debugger.CurrentProgram.Threads)
                {
                    var frames = new List<string>();
                    foreach (EnvDTE.StackFrame frame in thread.StackFrames) { frames.Add(frame.FunctionName); if (frames.Count == 16) break; }
                    threads.Add(new { thread.ID, Frames = frames });
                }
                Console.WriteLine(JsonSerializer.Serialize(threads));
            }
            else if (action == "break") { dte.Debugger.Break(false); Console.WriteLine("Break"); }
            else if (action == "step") { dte.Debugger.StepOver(false); Console.WriteLine("StepOver"); }
            else if (action == "continue") { dte.Debugger.Go(false); Console.WriteLine("Continue"); }
            else if (action == "detach")
            {
                int expected = int.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "TargetPid.txt")));
                var targets = new List<EnvDTE.Process>();
                foreach (EnvDTE.Process process in dte.Debugger.DebuggedProcesses)
                    if (process.ProcessID == expected) targets.Add(process);
                foreach (var target in targets) target.Detach(true);
                Console.WriteLine("Detached " + expected);
            }
            else if (action == "clear")
            {
                var remove = new List<object>();
                foreach (EnvDTE.Breakpoint point in dte.Debugger.Breakpoints)
                    for (int index = 2; index + 1 < args.Length; index += 2)
                        if (string.Equals(point.File, Path.GetFullPath(args[index]), StringComparison.OrdinalIgnoreCase) && point.FileLine == int.Parse(args[index + 1]))
                        { remove.Add(point); break; }
                foreach (EnvDTE.Breakpoint point in remove) point.Delete();
                Console.WriteLine("Cleared acceptance breakpoints");
            }
            else throw new InvalidOperationException("Unknown action");
            return 0;
        }
        catch (Exception exception) { Console.Error.WriteLine(exception.ToString()); return 1; }
        finally { CoRegisterMessageFilter(previous, out _); GC.KeepAlive(filter); }
    }
}
