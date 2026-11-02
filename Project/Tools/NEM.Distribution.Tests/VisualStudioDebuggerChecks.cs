using System.Diagnostics;
using System.IO.Compression;
using System.Runtime.ExceptionServices;
using System.Text.Json.Nodes;
using NEMEngine.Distribution;
using NEMEngine.ProjectTool;

// 実際のVisual Studioと配布EditorでScriptの停止を確認する
internal static class VisualStudioDebuggerChecks {
    public static void CloseGeneratedSessions(string repository) => RunSTA(() => {
        foreach (string directory in Directory.EnumerateDirectories(Path.Combine(repository, "Generated/LT"))) {
            string solution = Path.Combine(directory, "IDE/Projects/デバッグ確認/デバッグ確認.slnx");
            using var session = VisualStudioSession.FindOpen(solution);
            if (session == null) continue;
            session.Studio.Solution.Close(false);
            session.Studio.Quit();
        }
    });

    public static async Task RunAsync(string repository, string root, string version, bool contextOnly, Action<bool, string> check) {
        string reports = Path.Combine(repository, "Generated/LauncherDebuggerReport");
        Directory.CreateDirectory(reports);
        string engineRoot = Path.Combine(root, "IDE/Engine");
        ZipFile.ExtractToDirectory(Path.Combine(repository, "Generated/Distribution", version, "nemengine-" + version + "-win-x64.zip"), engineRoot);
        var manifest = EngineValidation.Read(engineRoot);
        string toolchain = new LauncherStore().State.Engines.First(entry => Directory.Exists(entry.ToolchainRoot)).ToolchainRoot;
        var engine = new InstalledEngine { Version = version, Root = engineRoot, ToolchainRoot = toolchain };
        var store = new LauncherStore(Path.Combine(root, "IDE/State"));
        store.Register(engine);
        var project = new ProjectService(store).Create("デバッグ確認", Path.Combine(root, "IDE/Projects"), version, "3D");
        string game = Path.GetDirectoryName(project.Descriptor)!;
        string script = Path.Combine(game, "GameAssets/DebuggerCheck.cs");
        const string source = """
            using NEMEngine;
            public sealed class DebuggerCheck : MonoBehaviour {
                private int counter;
                private void Update() {
                    // 一回分の更新を数える
                    int current = ++counter;
                    File.WriteAllText(Path.Combine(Application.ProjectRoot, "debugger-heartbeat.txt"), current.ToString());
                    // 検証完了後に通常終了する
                    if (File.Exists(Path.Combine(Application.ProjectRoot, "debugger-quit.txt"))) Application.Quit();
                }
            }
            """;
        File.WriteAllText(script, source);
        string guid = Guid.NewGuid().ToString("N");
        DistributionStorage.Write(script + ".meta", new {
            schemaVersion = 2, guid, type = "Script", importer = "ScriptImporter", importerVersion = 1, settings = new { }
        });
        string sdk = Path.Combine(toolchain, "dotnet.exe");
        var build = new ProcessStartInfo(sdk) { WorkingDirectory = game, UseShellExecute = false,
            RedirectStandardOutput = true, RedirectStandardError = true, CreateNoWindow = true };
        foreach (string arg in new[] { "build", Path.Combine(game, "Scripts/GameScripts.csproj"), "-c", "Develop", "-v:minimal", "-m:1" }) build.ArgumentList.Add(arg);
        using (var process = Process.Start(build)!) {
            Task<string> output = process.StandardOutput.ReadToEndAsync();
            Task<string> error = process.StandardError.ReadToEndAsync();
            await process.WaitForExitAsync();
            File.WriteAllText(Path.Combine(reports, "Script.Build.log"), await output + await error);
            check(process.ExitCode == 0, "デバッガ確認用Scriptのビルドに失敗しました");
        }
        string typeID = JsonNode.Parse(File.ReadAllText(script + ".meta"))!["scripts"]![0]!["scriptTypeId"]!.GetValue<string>();
        string scenePath = Path.Combine(game, "GameAssets/Scenes/Main.scene.json");
        var scene = JsonNode.Parse(File.ReadAllText(scenePath))!;
        scene["Entities"]![0]!["Components"]!["Script"] = new JsonArray(new JsonObject {
            ["enabled"] = true, ["lastKnownTypeName"] = "DebuggerCheck", ["scriptAsset"] = guid,
            ["scriptTypeId"] = typeID, ["scriptSlotId"] = "0000000000000100", ["serializedFields"] = new JsonObject { ["fields"] = new JsonObject() }
        });
        DistributionStorage.Write(scenePath, scene);
        Process? editor = null;
        Task<string>? editorOutput = null, editorError = null;
        string gate = Path.Combine(game, "debugger-play.txt");
        try {
            if (!contextOnly) {
                // 既存Projectの更新時と同じく不足した文字集合を起動時に補う
                DistributionStorage.RemoveTree(Path.Combine(game, "GameAssets/Fonts"), game);
                var start = new ProcessStartInfo(DistributionStorage.Child(engineRoot, manifest.Editor)) {
                    WorkingDirectory = game, UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true, CreateNoWindow = true
                };
                foreach (string arg in new[] { "--project", project.Descriptor, "--engine-root", engineRoot, "--toolchain-root", toolchain }) start.ArgumentList.Add(arg);
                start.Environment["NEMENGINE_ROOT"] = engineRoot;
                start.Environment["NEMENGINE_TOOLCHAIN_ROOT"] = toolchain;
                start.Environment["DOTNET_ROOT"] = toolchain;
                start.Environment["DOTNET_HOST_PATH"] = sdk;
                start.Environment["NEMENGINE_USER_SETTINGS_ROOT"] = Path.Combine(root, "IDE/User");
                start.Environment["NEM_RENDER_BENCHMARK"] = Path.Combine(reports, "Editor.json");
                start.Environment["NEM_RENDER_BENCHMARK_QUICK"] = "1";
                start.Environment["NEM_RENDER_BENCHMARK_WIDTH"] = "640";
                start.Environment["NEM_RENDER_BENCHMARK_HEIGHT"] = "360";
                start.Environment["NEM_RENDER_BENCHMARK_FPS"] = "30";
                start.Environment["NEM_RENDER_BENCHMARK_PLAY_GATE"] = gate;
                editor = Process.Start(start)!;
                editorOutput = editor.StandardOutput.ReadToEndAsync();
                editorError = editor.StandardError.ReadToEndAsync();
                await WaitAsync(() => File.Exists(gate + ".ready") || editor.HasExited);
                check(!editor.HasExited, "接続前にEditorが終了しました");
                check(File.ReadAllText(Path.Combine(game, "GameAssets/Fonts/Charset/default_charset.txt")).Contains("0x7e"), "Editor起動時にFontの文字集合をGameAssetsへ補完していません");
            }
            string solution = Path.ChangeExtension(project.Descriptor, ".slnx");
            // ProjectPanelと同じ配布ツールの入口から開く
            var open = new ProcessStartInfo(sdk) { WorkingDirectory = game, UseShellExecute = false,
                RedirectStandardOutput = true, RedirectStandardError = true, CreateNoWindow = true };
            foreach (string arg in new[] { Path.Combine(engineRoot, manifest.ProjectTool), "open", "--project", project.Descriptor,
                "--engine-root", engineRoot, "--toolchain-root", toolchain, "--file", script, "--line", "6" }) open.ArgumentList.Add(arg);
            if (editor != null) { open.ArgumentList.Add("--editor-pid"); open.ArgumentList.Add(editor.Id.ToString()); }
            using (var tool = Process.Start(open)!) {
                Task<string> output = tool.StandardOutput.ReadToEndAsync(), error = tool.StandardError.ReadToEndAsync();
                await tool.WaitForExitAsync();
                string message = await output + await error;
                File.WriteAllText(Path.Combine(reports, "ProjectTool.log"), message);
                check(tool.ExitCode == 0, "配布プロジェクトツールでScriptを開けません: " + message);
            }
            using (var tool = Process.Start(open)!) {
                Task<string> output = tool.StandardOutput.ReadToEndAsync(), error = tool.StandardError.ReadToEndAsync();
                await tool.WaitForExitAsync();
                string message = await output + await error;
                File.WriteAllText(Path.Combine(reports, "ProjectTool.Reopen.log"), message);
                check(tool.ExitCode == 0, "起動済みVisual StudioでScriptを再度開けません: " + message);
            }
            // 検証側のSTAでもVisual StudioのInteropを解決する
            _ = new VisualStudioService();
            RunSTA(() => {
                using var session = VisualStudioSession.Open(solution);
                dynamic studio = session.Studio;
                try {
                    session.OpenScript(script, 6);
                    object rawItem = studio.ActiveDocument.ProjectItem ?? throw new InvalidOperationException("開いたScriptにProjectItemがありません");
                    dynamic item = rawItem;
                    object rawProject = item.ContainingProject ?? throw new InvalidOperationException("開いたScriptに所属Projectがありません");
                    dynamic owner = rawProject;
                    DistributionStorage.Write(Path.Combine(reports, "Open.json"), new { solution = (string)studio.Solution.FullName, document = (string)studio.ActiveDocument.FullName, project = (string)owner.FullName });
                    check(Path.GetFullPath((string)owner.FullName) == Path.GetFullPath(Path.Combine(game, "Scripts/GameScripts.csproj")), "ScriptがGameScriptsへ所属していません");
                    bool resolved = false;
                    foreach (dynamic reference in owner.Object.References) {
                        if ((string)reference.Name == "NEM.ScriptCore") resolved = File.Exists((string)reference.Path);
                    }
                    check(resolved, "IntelliSense用のScriptCore参照を解決できません");
                    string language = studio.ActiveDocument.Language;
                    check(language is "CSharp" or "C#", "ScriptがC#として開かれていません: " + language);
                    string interopPath = contextOnly ? "" : File.ReadLines(Path.Combine(reports, "ProjectTool.log"))
                        .Single(line => line.StartsWith("Visual Studio Interop: ", StringComparison.Ordinal))["Visual Studio Interop: ".Length..];
                    if (!contextOnly) check(interopPath.Contains("Microsoft Visual Studio", StringComparison.OrdinalIgnoreCase), "InteropをVisual Studioから取得していません");
                    if (!contextOnly) check(Path.GetFullPath(interopPath).Equals(Path.GetFullPath(Path.Combine(Path.GetDirectoryName((string)studio.FullName)!, "PublicAssemblies/Microsoft.VisualStudio.Interop.dll")), StringComparison.OrdinalIgnoreCase),
                        "接続中のVisual StudioとInteropのインストール先が一致しません");
                    DistributionStorage.Write(Path.Combine(reports, "Context.json"), new { project = (string)owner.FullName, document = (string)studio.ActiveDocument.FullName, resolved, language, interopPath });
                    Console.WriteLine("PASS Visual Studio project context / ScriptCore reference");
                    CheckCompletion(studio, reports, check);
                    session.OpenScript(script, 6);
                    if (!contextOnly) {
                        session.Attach(editor!.Id);
                        dynamic breakpoints = studio.Debugger.Breakpoints.Add("", script, 6, 1, "", 1, "C#", "", 1, "", 0, 1);
                        File.WriteAllText(gate, "play");
                        VisualStudioSession.Wait<object>(() => (int)studio.Debugger.CurrentMode == 2 ? new object() : null, "Updateのブレークポイントへ停止しません");
                        string function = studio.Debugger.CurrentStackFrame.FunctionName;
                        check(function.Contains("DebuggerCheck.Update", StringComparison.Ordinal), "停止先がUpdateではありません: " + function);
                        studio.Debugger.StepOver(true);
                        string counter = studio.Debugger.GetExpression("counter", false, 5000).Value;
                        check(counter == "1", "ステップ実行後のcounterが1ではありません: " + counter);
                        DistributionStorage.Write(Path.Combine(reports, "Breakpoint.json"), new { stopped = true, function, counter, line = (int)studio.ActiveDocument.Selection.CurrentLine });
                        foreach (dynamic breakpoint in breakpoints) breakpoint.Delete();
                        File.WriteAllText(Path.Combine(game, "debugger-quit.txt"), "quit");
                        File.Delete(gate);
                        studio.Debugger.Go(false);
                        Console.WriteLine("PASS Update breakpoint / step over / counter=1");
                        VisualStudioSession.Wait<object>(() => editor!.HasExited ? new object() : null, "デバッガ確認後にEditorが終了しません");
                        check(editor!.ExitCode == 0, "Editorが正常終了していません");
                    }
                } finally {
                    // この検証で開いたSolutionだけを終了する
                    if (editor == null || editor.HasExited) {
                        CloseSession(session);
                    }
                }
            });
        } finally {
            if (editor != null) {
                if (!editor.HasExited) editor.Kill(true);
                await editor.WaitForExitAsync();
                RunSTA(() => {
                    using var session = VisualStudioSession.FindOpen(Path.ChangeExtension(project.Descriptor, ".slnx"));
                    if (session == null) return;
                    CloseSession(session);
                });
                File.WriteAllText(Path.Combine(reports, "Editor.log"), await editorOutput! + await editorError!);
                editor.Dispose();
            }
        }
    }

    private static void CloseSession(VisualStudioSession session) {
        VisualStudioSession.Wait<object>(() => { session.Studio.Solution.Close(false); return new object(); }, "検証用Solutionを閉じられません");
        VisualStudioSession.Wait<object>(() => { session.Studio.Quit(); return new object(); }, "検証用Visual Studioを終了できません");
    }

    private static async Task WaitAsync(Func<bool> complete) {
        var timer = Stopwatch.StartNew();
        while (!complete()) {
            if (timer.Elapsed > TimeSpan.FromMinutes(2)) throw new TimeoutException("Editorの起動を確認できません");
            await Task.Delay(500);
        }
    }

    private static void CheckCompletion(dynamic studio, string reports, Action<bool, string> check) {
        // 検証用の未保存バッファで実際の補完結果を読む
        dynamic document = studio.ActiveDocument;
        dynamic selection = document.Selection;
        const string expression = "        var point = transform.positio;";
        selection.GotoLine(6, true);
        selection.Text = expression;
        selection.MoveToLineAndOffset(6, expression.Length, false);
        studio.ExecuteCommand("Edit.CompleteWord", "");
        studio.ExecuteCommand("Edit.InsertTab", "");
        string completed = VisualStudioSession.Wait<string>(() => {
            dynamic text = document.Object("TextDocument");
            string content = text.StartPoint.CreateEditPoint().GetText(text.EndPoint);
            DistributionStorage.Write(Path.Combine(reports, "CompletionBuffer.json"), new { content, line = (int)selection.CurrentLine, column = (int)selection.CurrentColumn });
            return content.Contains("transform.position", StringComparison.Ordinal) ? content : null;
        }, "Visual StudioでTransform.positionを補完できません");
        check(completed.Contains("transform.position", StringComparison.Ordinal), "C#の補完結果が不正です");
        DistributionStorage.Write(Path.Combine(reports, "Completion.json"), new { expression, completed = "transform.position", verified = true });
        // 元のScriptとPDBの対応は変更しない
        document.Close(2);
        Console.WriteLine("PASS Visual Studio IntelliSense transform.position");
    }

    private static void RunSTA(Action action) {
        Exception? failure = null;
        var thread = new Thread(() => { try { action(); } catch (Exception error) { failure = error; } });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        thread.Join();
        if (failure != null) ExceptionDispatchInfo.Capture(failure).Throw();
    }
}
