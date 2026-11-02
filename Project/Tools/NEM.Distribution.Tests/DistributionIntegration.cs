using System.Diagnostics;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json.Nodes;
using NEMEngine.Distribution;

// 配布物から生成・起動・製品化までを確認する
internal static class DistributionIntegration {
    public static async Task RunAsync(string repository, string root, Action<bool, string> check, string version) {
        root = Path.Combine(root, "I");
        DistributionStorage.RequireVersion(version);
        string artifacts = Path.Combine(repository, "Generated/Distribution", version);
        var release = DistributionStorage.Read<EngineRelease>(Path.Combine(artifacts, "release-" + version + ".json"));
        using var client = new HttpClient(new LocalPackages(artifacts, release));
        var store = new LauncherStore(Path.Combine(root, "state"));
        store.State.InstallRoot = Path.Combine(root, "D");
        store.SaveSettings();
        var engine = await new EngineInstallService(store, client).InstallAsync(release, false, null, default);
        var service = new ProjectService(store);
        var project = service.Create("確認ゲーム", Path.Combine(root, "Projects"), release.Version, "3D");
        var project2D = service.Create("Game2D", Path.Combine(root, "Projects"), release.Version, "2D");
        string game = Path.GetDirectoryName(project.Descriptor)!;
        string sdk = Path.Combine(engine.ToolchainRoot, "dotnet.exe");
        string shell = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "WindowsPowerShell/v1.0/powershell.exe");
        await RunAsync(shell, ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", Path.Combine(repository, "Tools/Distribution/TestPowerShell.ps1"), "-Root", engine.Root],
            repository, new(), Path.Combine(repository, "Generated/Launcher.PowerShell.Tests.log"));
        check(true, "Windows PowerShellでの構文とInterop定義");
        string script = Path.Combine(game, "GameAssets/LauncherSmoke.cs");
        string scriptGUID = Guid.NewGuid().ToString("N");
        File.WriteAllText(script, """
            using NEMEngine;
            public sealed class LauncherSmoke : MonoBehaviour {
                [SerializeField] private float speed = 2;
                private void Update() {
                    // 実行中の保存値へアクセスする
                    transform.Translate(new Vector3(speed * Time.deltaTime, 0, 0), Space.World);
                    if (Time.frameCount < 40 || !File.Exists(Path.Combine(Application.ProjectRoot, ".nemBuildManifest.json"))) return;
                    // 製品のC#実行を記録して通常終了する
                    File.WriteAllText(Path.Combine(Application.persistentDataPath, "launcher-smoke.txt"), speed.ToString(System.Globalization.CultureInfo.InvariantCulture));
                    Application.Quit();
                }
            }
            """);
        DistributionStorage.Write(script + ".meta", new {
            schemaVersion = 2, guid = scriptGUID, type = "Script", importer = "ScriptImporter", importerVersion = 1, settings = new { }
        });
        string reports = Path.Combine(repository, "Generated/LauncherIntegrationReport");
        Directory.CreateDirectory(reports);
        var environment = new Dictionary<string, string> {
            ["NEMENGINE_ROOT"] = engine.Root, ["NEMENGINE_TOOLCHAIN_ROOT"] = engine.ToolchainRoot,
            ["DOTNET_ROOT"] = engine.ToolchainRoot, ["DOTNET_HOST_PATH"] = sdk,
            ["DOTNET_CLI_HOME"] = Path.Combine(root, "CLIHome"),
            ["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1", ["DOTNET_GENERATE_ASPNET_CERTIFICATE"] = "false", ["DOTNET_NOLOGO"] = "1",
            ["NEMENGINE_USER_SETTINGS_ROOT"] = Path.Combine(root, "UserSettings"),
            ["PATH"] = Environment.GetFolderPath(Environment.SpecialFolder.System)
        };
        await RunAsync(sdk, ["build", Path.Combine(game, "Scripts/GameScripts.csproj"), "-c", "Develop", "-v:minimal", "-m:1", "-nodeReuse:false"], game, environment, Path.Combine(reports, "CSharp.Build.log"));
        check(File.Exists(Path.Combine(game, "Managed/Develop/GameScripts.dll")), "同梱SDKでC#をコンパイルできません");
        check(File.Exists(Path.Combine(game, "Managed/Develop/GameScripts.pdb")), "C#のPDBがありません");
        // VisualStudioのDebugビルド後にPlay用の出力先を切り替える
        await RunAsync(sdk, ["build", Path.Combine(game, "Scripts/GameScripts.csproj"), "-c", "Debug", "-v:minimal"], game, environment, Path.Combine(reports, "CSharp.Debug.Build.log"));
        for (int cycle = 1; cycle <= 2; ++cycle) {
            string staging = Path.Combine(game, "Managed/Staging/Develop", cycle.ToString());
            await RunAsync(sdk, ["build", Path.Combine(game, "Scripts/GameScripts.csproj"), "-c", "Develop", "--no-dependencies", "--no-incremental",
                "-p:NEMScriptIntermediateOutput=" + Path.Combine(staging, "obj").Replace('\\', '/') + "/",
                "-p:NEMScriptStagingOutput=" + staging, "-p:NEMSkipMetadataSync=true", "-p:Optimize=false", "-p:DebugType=portable", "-p:DebugSymbols=true"],
                game, environment, Path.Combine(reports, "CSharp.Play" + cycle + ".Build.log"));
            check(File.Exists(Path.Combine(staging, "GameScripts.dll")) && File.Exists(Path.Combine(staging, "GameScripts.pdb")),
                "通常ビルド後のPlayビルドでDLLとPDBを生成できません");
        }
        var metadata = JsonNode.Parse(File.ReadAllText(script + ".meta"))!;
        string typeID = metadata["scripts"]![0]!["scriptTypeId"]!.GetValue<string>();
        check(Guid.TryParse(typeID, out _), "初回ビルドでScriptのIDを保存できません");
        string scenePath = Path.Combine(game, "GameAssets/Scenes/Main.scene.json");
        var scene = JsonNode.Parse(File.ReadAllText(scenePath))!;
        scene["Entities"]![0]!["Components"]!["Script"] = new JsonArray(new JsonObject {
            ["enabled"] = true, ["lastKnownTypeName"] = "LauncherSmoke", ["scriptAsset"] = scriptGUID,
            ["scriptTypeId"] = typeID, ["scriptSlotId"] = "0000000000000100", ["serializedFields"] = new JsonObject { ["fields"] = new JsonObject() }
        });
        DistributionStorage.Write(scenePath, scene);
        // 画面入力を使わず、既存の描画検証経路で通常終了する
        var benchmark = new Dictionary<string, string>(environment) {
            ["NEM_RENDER_BENCHMARK"] = Path.Combine(reports, "Editor3D.json"),
            ["NEM_RENDER_BENCHMARK_QUICK"] = "1", ["NEM_RENDER_BENCHMARK_PHASE_COUNT"] = "1",
            ["NEM_RENDER_BENCHMARK_CAPTURE"] = "Final", ["NEM_RENDER_BENCHMARK_WIDTH"] = "640", ["NEM_RENDER_BENCHMARK_HEIGHT"] = "360"
        };
        var manifest = EngineValidation.Read(engine.Root);
        string[] editorArguments = ["--project", project.Descriptor, "--engine-root", engine.Root, "--toolchain-root", engine.ToolchainRoot];
        File.Delete(benchmark["NEM_RENDER_BENCHMARK"]);
        await RunAsync(DistributionStorage.Child(engine.Root, manifest.Editor), editorArguments, game, benchmark, Path.Combine(reports, "Editor3D.log"));
        check(File.Exists(benchmark["NEM_RENDER_BENCHMARK"]), "配布Editorの3D描画確認が完了していません");
        benchmark["NEM_RENDER_BENCHMARK"] = Path.Combine(reports, "Editor2D.json");
        File.Delete(benchmark["NEM_RENDER_BENCHMARK"]);
        editorArguments[1] = project2D.Descriptor;
        await RunAsync(DistributionStorage.Child(engine.Root, manifest.Editor), editorArguments, Path.GetDirectoryName(project2D.Descriptor)!, benchmark, Path.Combine(reports, "Editor2D.log"));
        check(File.Exists(benchmark["NEM_RENDER_BENCHMARK"]), "配布Editorの2D描画確認が完了していません");
        string buildManifest = await WriteBuildManifestAsync(project, engine, Path.Combine(root, "Products"));
        await RunAsync(shell, ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", Path.Combine(engine.Root, "Tools/BuildGame.ps1"), "-ManifestPath", buildManifest], game, environment, Path.Combine(reports, "Product.Build.log"));
        string product = Path.Combine(root, "Products/LauncherSmoke");
        check(File.Exists(Path.Combine(product, "LauncherSmoke.exe")) && !Directory.Exists(Path.Combine(product, "DotNet/sdk")), "製品へSDKが混入しています");
        var productEnvironment = new Dictionary<string, string> {
            ["NEMENGINE_ROOT"] = engine.Root, ["NEMENGINE_TOOLCHAIN_ROOT"] = Path.Combine(root, "MissingSDK"),
            ["DOTNET_ROOT"] = "", ["DOTNET_HOST_PATH"] = "",
            ["PATH"] = Environment.GetFolderPath(Environment.SpecialFolder.System), ["NEMENGINE_USER_SETTINGS_ROOT"] = Path.Combine(root, "ProductUser")
        };
        await RunAsync(Path.Combine(product, "LauncherSmoke.exe"), [], repository, productEnvironment, Path.Combine(reports, "Product.Run.log"));
        string marker = Directory.EnumerateFiles(Path.Combine(root, "ProductUser"), "launcher-smoke.txt", SearchOption.AllDirectories).Single();
        check(File.ReadAllText(marker) == "2", "製品でC#の更新または保存Fieldの初期化が正しくありません");
        check(!Directory.EnumerateFiles(game, "*.vcxproj", SearchOption.AllDirectories).Any(), "C++プロジェクトが生成されています");
        await EngineValidation.VerifyAsync(engine.Root, manifest, null, default);
        check(true, "共有エンジンの内容保持");
    }

    private static async Task RunAsync(string executable, string[] arguments, string directory,
        Dictionary<string, string> environment, string log) {
        var start = new ProcessStartInfo(executable) { WorkingDirectory = directory, UseShellExecute = false,
            CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (string argument in arguments) start.ArgumentList.Add(argument);
        foreach (var value in environment) start.Environment[value.Key] = value.Value;
        using var process = Process.Start(start) ?? throw new IOException("開始できません: " + executable);
        Task<string> output = process.StandardOutput.ReadToEndAsync(), error = process.StandardError.ReadToEndAsync();
        using var timeout = new CancellationTokenSource(TimeSpan.FromMinutes(4));
        try { await process.WaitForExitAsync(timeout.Token); }
        catch (OperationCanceledException) { process.Kill(true); await process.WaitForExitAsync(); throw new IOException("検証の待機時間を超えました: " + executable); }
        finally { File.WriteAllText(log, await output + "\n" + await error); }
        if (process.ExitCode != 0) throw new IOException("検証に失敗しました: " + executable + " 終了コード=" + process.ExitCode + " ログ=" + log);
    }

    private static async Task<string> WriteBuildManifestAsync(ProjectEntry project, InstalledEngine engine, string output) {
        string game = Path.GetDirectoryName(project.Descriptor)!;
        var files = new List<object>();
        var entries = new SortedDictionary<string, string>(StringComparer.Ordinal);
        foreach (var item in new[] { (Path.Combine(game, "GameAssets"), "GameAssets"), (Path.Combine(engine.Root, "Engine/Assets"), "Engine/Assets"), (Path.Combine(game, "ProjectSettings"), "ProjectSettings") })
            foreach (string file in Directory.EnumerateFiles(item.Item1, "*", SearchOption.AllDirectories))
                entries[item.Item2 + "/" + Path.GetRelativePath(item.Item1, file).Replace('\\', '/')] = file;
        var signature = new StringBuilder();
        foreach (var entry in entries) {
            long size = new FileInfo(entry.Value).Length;
            string hash = await DistributionStorage.HashAsync(entry.Value);
            files.Add(new { source = entry.Value, destination = entry.Key, size, sha256 = hash });
            signature.Append(entry.Key).Append('\0').Append(hash).Append('\0').Append(size).Append('\n');
        }
        string path = Path.Combine(game, "Library/IntegrationBuild.json");
        var descriptor = JsonNode.Parse(File.ReadAllText(project.Descriptor))!;
        DistributionStorage.Write(path, new {
            schemaVersion = 2, prebuiltRuntime = true, projectPath = project.Descriptor, gameRoot = game,
            sourceRuntime = Path.Combine(engine.Root, "Runtime/Release"), runtimeExecutable = "NEMPlayer.exe",
            buildToolProject = "", buildToolExecutable = Path.Combine(engine.Root, "Tools/NEMBuildTool/NEMBuildTool.exe"),
            toolchainRoot = engine.ToolchainRoot, outputRoot = output, productName = "LauncherSmoke", executableName = "LauncherSmoke.exe",
            projectGuid = descriptor["projectGuid"]!.GetValue<string>(), startupScene = "4e454d50524f4a4543544d41494e0001",
            startupFullscreen = false, gameWidth = 640, gameHeight = 360, packages = Array.Empty<object>(), files,
            confirmedWarnings = Array.Empty<object>(), cookHash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(signature.ToString()))).ToLowerInvariant()
        });
        return path;
    }

    private sealed class LocalPackages(string directory, EngineRelease release) : HttpMessageHandler {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token) {
            var package = request.RequestUri!.AbsoluteUri == release.Engine.Url ? release.Engine : release.Toolchain;
            var stream = File.OpenRead(Path.Combine(directory, package.ID + ".zip"));
            return Task.FromResult(new HttpResponseMessage(HttpStatusCode.OK) { Content = new StreamContent(stream) });
        }
    }
}
