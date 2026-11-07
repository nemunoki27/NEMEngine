using System.IO.Compression;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using NEMEngine.Distribution;

// サンプルの取得から作成までとZIP保存の失敗時を確認する
internal static class ContentDistributionChecks {
    public static async Task RunAsync(string root, Action<bool, string> check) {
        string directory = Path.Combine(root, "Content");
        var store = CreateStore(directory);
        string projects = Path.Combine(directory, "Projects");
        var files = new Dictionary<string, string> {
            ["template.json"] = JsonSerializer.Serialize(new ProjectTemplateManifest { ID = "sample", Kind = "Sample", Name = "昼夜の確認",
                StartupScene = "11111111111111111111111111111111" }, DistributionStorage.JsonOptions),
            ["GameAssets/Scenes/Sample.scene.json"] = "{\"Entities\":[]}",
            ["GameAssets/Scenes/Sample.scene.json.meta"] = "{\"guid\":\"11111111111111111111111111111111\"}",
            ["GameAssets/Scripts/Actor.cs"] = "// 日本語のスクリプト\nclass Actor {}",
            ["GameAssets/Scripts/Actor.cs.meta"] = "{\"guid\":\"22222222222222222222222222222222\"}",
            ["ProjectSettings/Runtime/FrameRate.json"] = "{\"limit\":120}",
            ["Scripts/GameScripts.csproj"] = "<Project />"
        };
        byte[] zip = MakeZIP(files);
        var package = Package(zip, files);
        var sample = new SampleRelease { ID = "sample", Name = "昼夜の確認", Version = "1.0.0", EngineVersion = "1.2.3", Package = package };
        var asset = new AssetRelease { ID = "asset", Name = "テクスチャ", Version = "1.0.0", FileName = "textures.zip", Package = package };
        int requests = 0;
        using var client = new HttpClient(new Reply(_ => { ++requests; return new(HttpStatusCode.OK) { Content = new ByteArrayContent(zip) }; }));
        var service = new ProjectService(store);
        var project = await service.CreateSampleAsync("Sample1", projects, "1.2.3", sample, client, null, default);
        string game = Path.GetDirectoryName(project.Descriptor)!;
        check(File.ReadAllText(Path.Combine(game, "GameAssets/Scripts/Actor.cs")).Contains("日本語"), "サンプルのスクリプトがありません");
        check(File.ReadAllText(Path.Combine(game, "GameAssets/Scenes/Sample.scene.json.meta")).Contains("11111111111111111111111111111111"), "アセットのGUIDが変わりました");
        check(!File.Exists(Path.Combine(game, "template.json")), "配布元の情報をゲームへコピーしました");
        check(File.ReadAllText(Path.Combine(game, "ProjectSettings/Runtime/StartupScene.json")).Contains("11111111111111111111111111111111"), "開始シーンを保存しません");
        check(File.ReadAllText(Path.Combine(game, "ProjectSettings/Runtime/FrameRate.json")) == files["ProjectSettings/Runtime/FrameRate.json"], "共有設定へスナップショットを重ねていません");
        var second = await service.CreateSampleAsync("Sample2", projects, "1.2.3", sample, client, null, default);
        check(second.ProjectGUID != project.ProjectGUID && requests == 1, "プロジェクトGUIDを再利用したか、取得済みZIPを再取得しました");
        await Reject(() => service.CreateSampleAsync("WrongEngine", projects, "1.2.3", sample with { EngineVersion = "1.2.4" }, client, null, default), check);
        check(requests == 1 && !Directory.Exists(Path.Combine(projects, "WrongEngine")), "非対応版でも取得またはプロジェクト作成しました");
        await Reject(() => service.CreateSampleAsync("WrongID", projects, "1.2.3", sample with { ID = "different" }, client, null, default), check);
        check(!Directory.Exists(Path.Combine(projects, "WrongID")) && store.State.Projects.Count == 2, "失敗したサンプルを公開または登録しました");
        using (var cancellation = new CancellationTokenSource()) {
            var progress = new Progress(value => { if (value.Message == "展開中") cancellation.Cancel(); });
            await Reject(() => service.CreateSampleAsync("Cancelled", projects, "1.2.3", sample, client, progress, cancellation.Token), check);
        }
        check(!Directory.Exists(Path.Combine(projects, "Cancelled")) && store.State.Projects.Count == 2, "キャンセルしたプロジェクトが残っています");
        check(!Directory.EnumerateDirectories(Path.Combine(store.Root, "Downloads"), ".sample-*").Any(), "展開失敗後に作業領域が残っています");

        var packageFiles = new Dictionary<string, string>(files) {
            ["template.json"] = JsonSerializer.Serialize(new ProjectTemplateManifest { ID = "sample", Kind = "Sample", Name = "Packageシーン",
                StartupScene = "33333333333333333333333333333333" }, DistributionStorage.JsonOptions),
            ["Packages/Stage/Scenes/Main.scene.json"] = "{\"Entities\":[]}",
            ["Packages/Stage/Scenes/Main.scene.json.meta"] = "{\"guid\":\"33333333333333333333333333333333\"}"
        };
        byte[] packageZIP = MakeZIP(packageFiles);
        using var packageClient = new HttpClient(new Reply(_ => new(HttpStatusCode.OK) { Content = new ByteArrayContent(packageZIP) }));
        var packageProject = await service.CreateSampleAsync("PackageScene", projects, "1.2.3", sample with { Package = Package(packageZIP, packageFiles) }, packageClient, null, default);
        check(File.Exists(Path.Combine(Path.GetDirectoryName(packageProject.Descriptor)!, "Packages/Stage/Scenes/Main.scene.json")),
            "Package内の開始シーンを利用できません");

        string output = Path.Combine(directory, "保存したアセット.zip");
        var download = new AssetDownloadService(client);
        await download.SaveAsync(asset, Path.Combine(store.Root, "Downloads"), output, false, null, default);
        check(await DistributionStorage.HashAsync(output) == package.Sha256, "保存したZIPの内容が異なります");
        await Reject(() => download.SaveAsync(asset, Path.Combine(store.Root, "Downloads"), output, false, null, default), check);
        await File.WriteAllTextAsync(output, "existing");
        using (var cancellation = new CancellationTokenSource()) {
            var progress = new Progress(value => { if (value.Message == "ZIPを保存中") cancellation.Cancel(); });
            await Reject(() => download.SaveAsync(asset, Path.Combine(store.Root, "Downloads"), output, true, progress, cancellation.Token), check);
        }
        check(File.ReadAllText(output) == "existing" && !Directory.EnumerateFiles(directory, ".asset-*").Any(), "キャンセルで保存先を壊したか一時ファイルを残しました");
        await CheckCatalogAsync(sample, asset, check);
    }

    private static LauncherStore CreateStore(string directory) {
        string engine = Path.Combine(directory, "Engine"), sdk = Path.Combine(directory, "SDK");
        foreach (string file in new[] { "Editor/Develop/NEMEditor.exe", "Tools/ProjectTool/NEM.ProjectTool.dll", "ProjectTemplates/Common/Scripts/GameScripts.csproj" }) {
            string path = Path.Combine(engine, file);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, "fixture");
        }
        DistributionStorage.Write(Path.Combine(engine, "ProjectTemplates/Common/ProjectSettings/Runtime/FrameRate.json"), new { limit = 60 });
        Directory.CreateDirectory(sdk);
        File.WriteAllText(Path.Combine(sdk, "dotnet.exe"), "sdk");
        DistributionStorage.Write(Path.Combine(engine, "engine.json"), new EngineManifest { Version = "1.2.3", BuildID = "fixture", ToolchainVersion = "10.0.401" });
        var store = new LauncherStore(Path.Combine(directory, "State"));
        store.Register(new InstalledEngine { Version = "1.2.3", Root = engine, ToolchainRoot = sdk });
        return store;
    }

    private static async Task CheckCatalogAsync(SampleRelease sample, AssetRelease asset, Action<bool, string> check) {
        using var old = new HttpClient(new Reply(_ => JSON(new { schemaVersion = 1, engines = Array.Empty<EngineRelease>() })));
        var previous = await CatalogService.FetchAsync(old, "https://unit.invalid/catalog.json", default);
        check(previous.Samples.Count == 0 && previous.Assets.Count == 0, "以前の配布一覧を読み込めません");
        var catalog = new ReleaseCatalog { Samples = [sample], Assets = [asset] };
        using var client = new HttpClient(new Reply(_ => JSON(catalog)));
        var current = await CatalogService.FetchAsync(client, "https://unit.invalid/catalog.json", default);
        check(current.Samples.Single() == sample && current.Assets.Single() == asset, "新しい配布情報を往復できません");
        foreach (var invalid in new[] {
            catalog with { Samples = [sample, sample] },
            catalog with { Assets = [asset with { FileName = "../escape.zip" }] },
            catalog with { Samples = [sample with { EngineVersion = "1.2" }] },
            catalog with { Assets = [asset with { Package = asset.Package with { Sha256 = "invalid" } }] }
        }) {
            using var bad = new HttpClient(new Reply(_ => JSON(invalid)));
            await Reject(() => CatalogService.FetchAsync(bad, "https://unit.invalid/catalog.json", default), check);
        }
    }

    private static byte[] MakeZIP(Dictionary<string, string> files) {
        using var stream = new MemoryStream();
        using (var zip = new ZipArchive(stream, ZipArchiveMode.Create, true))
            foreach (var file in files) { using var writer = new StreamWriter(zip.CreateEntry(file.Key).Open(), new UTF8Encoding(false)); writer.Write(file.Value); }
        return stream.ToArray();
    }

    private static PackageInfo Package(byte[] zip, Dictionary<string, string> files) => new() {
        ID = "sample-fixture", Url = "https://unit.invalid/sample.zip", Bytes = zip.Length,
        UnpackedBytes = files.Values.Sum(value => Encoding.UTF8.GetByteCount(value)),
        Sha256 = Convert.ToHexString(SHA256.HashData(zip)).ToLowerInvariant()
    };

    private static HttpResponseMessage JSON<T>(T value) => new(HttpStatusCode.OK) {
        Content = new StringContent(JsonSerializer.Serialize(value, DistributionStorage.JsonOptions), Encoding.UTF8, "application/json")
    };

    private static async Task Reject(Func<Task> action, Action<bool, string> check) {
        try { await action(); }
        catch (Exception error) when (error is InvalidDataException or IOException or ArgumentException or OperationCanceledException) {
            check(true, "失敗時の処理");
            return;
        }
        throw new Exception("無効な操作を受け付けました");
    }

    private sealed class Progress(Action<TransferProgress> action) : IProgress<TransferProgress> {
        public void Report(TransferProgress value) => action(value);
    }
    private sealed class Reply(Func<HttpRequestMessage, HttpResponseMessage> response) : HttpMessageHandler {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken) => Task.FromResult(response(request));
    }
}
