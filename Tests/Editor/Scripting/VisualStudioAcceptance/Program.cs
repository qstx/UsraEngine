using System.Collections;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text.Json;

internal static class Program
{
    [DllImport("ole32.dll")] private static extern int GetRunningObjectTable(int reserved, out IRunningObjectTable table);
    [DllImport("ole32.dll")] private static extern int CreateBindCtx(int reserved, out IBindCtx context);

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

    private static object VerifyLanguage(EnvDTE.DTE dte, string path, string? definition)
    {
        if (dte.Debugger.CurrentMode != EnvDTE.dbgDebugMode.dbgDesignMode || dte.UndoContext.IsOpen)
            throw new InvalidOperationException("IDE language verification requires an idle solution.");
        EnvDTE.Document document = dte.ItemOperations.OpenFile(Path.GetFullPath(path)).Document;
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
                const string member = "LocalPosition";
                int offset = original.IndexOf("Transform.LocalPosition", StringComparison.Ordinal) + "Transform.".Length;
                if (offset < "Transform.".Length) throw new InvalidOperationException("Completion fixture not found.");
                Move(selection, original, offset);
                selection.MoveToLineAndOffset(selection.ActivePoint.Line, selection.ActivePoint.LineCharOffset + member.Length, true);
                selection.Text = "LocalRotat";
                Thread.Sleep(500);
                dte.ExecuteCommand("Edit.CompleteWord");
                DateTime deadline = DateTime.UtcNow.AddSeconds(12);
                bool completed = false;
                do
                {
                    string current = Retry(() => text.StartPoint.CreateEditPoint().GetText(text.EndPoint));
                    completed = current.Length >= offset + "LocalRotation".Length && current.Substring(offset, "LocalRotation".Length) == "LocalRotation";
                    if (completed) break;
                    Thread.Sleep(150);
                } while (DateTime.UtcNow < deadline);
                if (!completed) throw new InvalidOperationException("IDE did not complete the Engine Transform.LocalRotation member.");
                result = new { Action = "Completion", File = path, Typed = "LocalRotat", Completed = "LocalRotation" };
            }
            else
            {
                const string symbol = "MoveWithInputBehaviour";
                string extra = "\ninternal static class UrsaIdeNavigationProbe { internal static System.Type Target => typeof(MoveWithInputBehaviour); }\n";
                selection.EndOfDocument(false);
                selection.Text = extra;
                string current = text.StartPoint.CreateEditPoint().GetText(text.EndPoint);
                int offset = current.LastIndexOf(symbol, StringComparison.Ordinal);
                Move(selection, current, offset + 5);
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
                result = new { Action = "ProjectReferenceNavigation", Source = path, Target = actual };
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
        try
        {
            var dte = Dte(Path.GetFullPath(args[1]));
            var debugger = (EnvDTE80.Debugger2)dte.Debugger;
            string action = args[0];
            if (action is "state" or "evaluate" or "step" or "continue" or "detach")
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
            else if (action == "complete") Console.WriteLine(JsonSerializer.Serialize(VerifyLanguage(dte, args[2], null)));
            else if (action == "navigate-definition") Console.WriteLine(JsonSerializer.Serialize(VerifyLanguage(dte, args[2], args[3])));
            else if (action == "attach")
            {
                int pid = int.Parse(args[2]);
                if (dte.Debugger.DebuggedProcesses.Count != 0) throw new InvalidOperationException("IDE already has a debugger session; refusing to replace it.");
                foreach (EnvDTE.Process process in dte.Debugger.LocalProcesses)
                    if ((int)process.ProcessID == pid) { ((EnvDTE80.Process2)process).Attach2(args[3]); File.WriteAllText(Path.Combine(AppContext.BaseDirectory, "TargetPid.txt"), pid.ToString()); Console.WriteLine("Attached " + pid); return 0; }
                throw new InvalidOperationException("Target process was not listed");
            }
            else if (action == "breakpoint")
            {
                var points = dte.Debugger.Breakpoints.Add("", Path.GetFullPath(args[2]), int.Parse(args[3]), 1);
                Console.WriteLine("Breakpoint added: " + points.Count);
            }
            else if (action == "state")
            {
                int mode = (int)dte.Debugger.CurrentMode;
                string function = ""; string file = ""; int line = 0; var frames = new List<string>();
                if (mode == 2 && dte.Debugger.CurrentStackFrame is { } currentFrame)
                {
                    function = currentFrame.FunctionName;
                    foreach (EnvDTE.StackFrame frame in dte.Debugger.CurrentThread.StackFrames) { frames.Add(frame.FunctionName); if (frames.Count == 12) break; }
                    try { file = (string)dte.ActiveDocument.FullName; line = ((EnvDTE.TextSelection)dte.ActiveDocument.Selection).ActivePoint.Line; } catch { }
                }
                Console.WriteLine(JsonSerializer.Serialize(new { Mode = mode, Function = function, File = file, Line = line, Frames = frames }));
            }
            else if (action == "evaluate")
            {
                var expression = dte.Debugger.GetExpression(args[2], true, 1000);
                Console.WriteLine(JsonSerializer.Serialize(new { Expression = args[2], Valid = (bool)expression.IsValidValue, Value = (string)expression.Value }));
            }
            else if (action == "step") { dte.Debugger.StepOver(false); Console.WriteLine("StepOver"); }
            else if (action == "continue") { dte.Debugger.Go(false); Console.WriteLine("Continue"); }
            else if (action == "detach") { dte.Debugger.DetachAll(); Console.WriteLine("Detached"); }
            else if (action == "clear")
            {
                var remove = new List<object>();
                foreach (EnvDTE.Breakpoint point in dte.Debugger.Breakpoints)
                    if (((string)point.File).Contains("IdeAcceptance", StringComparison.OrdinalIgnoreCase)) remove.Add(point);
                foreach (EnvDTE.Breakpoint point in remove) point.Delete();
                Console.WriteLine("Cleared acceptance breakpoints");
            }
            else throw new InvalidOperationException("Unknown action");
            return 0;
        }
        catch (Exception exception) { Console.Error.WriteLine(exception.ToString()); return 1; }
    }
}
