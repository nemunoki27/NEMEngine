using System.Diagnostics;
using System.Runtime.InteropServices;
using NEMEngine.Distribution;

namespace NEMEngine.ProjectTool;

// ROTの列挙は型付きCOMで行い、DTEの操作を同じSTAへ揃える
internal sealed class VisualStudioSession(object studio) : IDisposable {
    internal dynamic Studio => studio;

    public static VisualStudioSession Open(string solution) {
        solution = Path.GetFullPath(solution);
        object? existing = Find(solution);
        try {
            var installations = VisualStudioDiscovery.Find();
            if (existing == null) {
                var selected = VisualStudioDiscovery.Select(installations);
                var start = new ProcessStartInfo(selected.Executable) { UseShellExecute = true };
                start.ArgumentList.Add(solution);
                using var launched = Process.Start(start) ?? throw new IOException("Visual Studioを起動できません");
                existing = Wait(() => Find(solution), "Visual Studioでプロジェクトを開けません");
            }
            // 起動済みInsidersも同じSolutionならそのまま使う
            dynamic instance = existing;
            string executable = Wait<string>(() => (string)instance.FullName, "Visual Studioの実行パスを取得できません");
            VisualStudioService.PrepareInterop(VisualStudioDiscovery.Select(installations, executable));
            return new(existing);
        } catch {
            if (existing != null) Marshal.ReleaseComObject(existing);
            throw;
        }
    }

    internal static VisualStudioSession? FindOpen(string solution) {
        object? instance = Find(Path.GetFullPath(solution));
        return instance == null ? null : new(instance);
    }

    private static object? Find(string solution) => VisualStudioInstances.Find(solution);

    public void OpenScript(string file, int line) {
        file = Path.GetFullPath(file);
        if (!File.Exists(file)) throw new FileNotFoundException("スクリプトが見つかりません", file);
        // その他のファイルではなく、GameScriptsのProjectItemを開く
        object item = Wait<object>(() => {
            dynamic? candidate = Studio.Solution.FindProjectItem(file);
            if (candidate == null) return null;
            object? rawProject = candidate.ContainingProject;
            if (rawProject == null) return null;
            dynamic owner = rawProject;
            string project = owner.FullName;
            return project.EndsWith(".csproj", StringComparison.OrdinalIgnoreCase) ? candidate : null;
        }, "GameScriptsの読込が完了しません C#開発環境を確認してください");
        dynamic projectItem = item;
        projectItem.Open("{7651A700-06E5-11D1-8EBD-00A0C90F26EA}").Activate();
        if (line > 1) Studio.ActiveDocument.Selection.GotoLine(line, true);
        Studio.MainWindow.Visible = true;
        Studio.UserControl = true;
        Studio.MainWindow.Activate();
    }

    public void Attach(int pid) {
        // 拡張されたデバッガAPIはDebugger2のInterfaceで呼ぶ
        var debugger = (EnvDTE80.Debugger2)Studio.Debugger;
        Console.WriteLine("Visual Studio: Editorの接続状態を確認");
        // 接続済みのEditorには二重接続しない
        foreach (EnvDTE.Process process in debugger.DebuggedProcesses)
            if ((int)process.ProcessID == pid) return;
        object target = Wait<object>(() => {
            foreach (EnvDTE80.Process2 process in debugger.LocalProcesses)
                if ((int)process.ProcessID == pid) return process;
            return null;
        }, "接続先Editorが見つかりません");
        Console.WriteLine("Visual Studio: .NETデバッガを取得");
        string? engineID = null;
        foreach (EnvDTE80.Transport transport in debugger.Transports) {
            foreach (EnvDTE80.Engine engine in transport.Engines) {
                string name = engine.Name;
                if (name.Contains(".NET", StringComparison.OrdinalIgnoreCase) &&
                    (name.Contains("Core", StringComparison.OrdinalIgnoreCase) || name.Contains("5+", StringComparison.Ordinal))) {
                    engineID = engine.ID;
                    break;
                }
            }
            if (engineID != null) break;
        }
        if (engineID == null) throw new IOException("Visual Studioの.NETデバッガが見つかりません C#開発環境を導入してください");
        var attachTarget = (EnvDTE80.Process2)target;
        Console.WriteLine("Visual Studio: Editorへ接続");
        attachTarget.Attach2(engineID);
        Console.WriteLine("Visual Studio: 接続完了を確認");
        Wait<object>(() => {
            foreach (EnvDTE.Process process in debugger.DebuggedProcesses)
                if ((int)process.ProcessID == pid) return process;
            return null;
        }, "Editorへのデバッガ接続が完了しません");
    }

    internal static T Wait<T>(Func<T?> action, string message) where T : class {
        var timer = Stopwatch.StartNew();
        Exception? last = null;
        while (timer.Elapsed < TimeSpan.FromSeconds(120)) {
            try {
                T? result = action();
                if (result != null) return result;
            } catch (COMException error) { last = error; }
            Thread.Sleep(300);
        }
        throw new IOException(message, last);
    }

    public void Dispose() => Marshal.ReleaseComObject(studio);
}
