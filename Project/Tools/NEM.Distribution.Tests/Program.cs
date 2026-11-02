using System.IO.Compression;
using System.Net;
using System.Net.Http.Headers;
using System.Security.Cryptography;
using NEMEngine.Distribution;

string repository = Path.GetFullPath(args.FirstOrDefault(value => !value.StartsWith("--")) ?? ".");
GitNativeChecks.LauncherOverride = args.FirstOrDefault(value => value.StartsWith("--launcher="))?[11..];
string owner = Path.Combine(repository, "Generated", "LT");
string root = Path.Combine(owner, Guid.NewGuid().ToString("N")[..8]);
Directory.CreateDirectory(root);
if (args.Contains("--cleanup-ide")) {
    VisualStudioDebuggerChecks.CloseGeneratedSessions(repository);
    return;
}
int passed = 0;
void Check(bool result, string message) {
    if (!result) throw new Exception(message);
    ++passed;
}
async Task Reject(Func<Task> action, string message) {
    try { await action(); } catch (Exception error) when (error is InvalidDataException or IOException or ArgumentException or InvalidOperationException) { ++passed; return; }
    throw new Exception(message);
}
try {
    LauncherThemeChecks.Run(repository, root, Check);
    ProjectDeletionChecks.Run(root, Check);
    ProjectRenameChecks.Run(root, Check);
    await GitHubContractChecks.RunAsync(root, Check);
    await GitHubAccountChecks.RunAsync(root, Check);
    await GitHubDestinationChecks.RunAsync(root, Check);
    await ProjectGitLinkChecks.RunAsync(root, Check);
    await BugReportChecks.RunAsync(root, Check);
    VisualStudioDiscoveryChecks.Run(root, Check);
    if (args.Contains("--github-owners")) await GitHubDestinationChecks.RunLiveAsync(Check);
    if (args.Contains("--git-native")) await GitNativeChecks.RunAsync(repository, root, Check);
    if (args.Contains("--git-live")) await GitLiveChecks.RunAsync(repository, root, Check);
    foreach (string path in new[] { "../escape", "x/../../escape", "/absolute", "C:/escape", "x:stream", "x./file" })
        await Reject(() => Task.FromResult(DistributionStorage.Child(root, path)), "危険なパスを受理しました: " + path);
    foreach (string version in new[] { "../1", "1.2", "1.2.3.4", "01.2.3", "1.2.3-beta" })
        await Reject(() => { DistributionStorage.RequireVersion(version); return Task.CompletedTask; }, "不正な版を受理しました");

    byte[] data = Enumerable.Range(0, 4096).Select(value => (byte)value).ToArray();
    string hash = Convert.ToHexString(SHA256.HashData(data)).ToLowerInvariant();
    var package = new PackageInfo { ID = "test", Url = "https://unit.invalid/test.zip", Sha256 = hash, Bytes = data.Length, UnpackedBytes = data.Length };
    string cache = Path.Combine(root, "cache");
    Directory.CreateDirectory(cache);
    string archive = Path.Combine(cache, "test-" + hash + ".zip");
    await File.WriteAllBytesAsync(archive + ".partial", data[..1024]);
    using var client = new HttpClient(new Reply(request => {
        Check(request.Headers.Range?.Ranges.Single().From == 1024, "再開位置が不正です");
        var response = new HttpResponseMessage(HttpStatusCode.PartialContent) { Content = new ByteArrayContent(data[1024..]) };
        response.Content.Headers.ContentRange = new ContentRangeHeaderValue(1024, data.Length - 1, data.Length);
        return response;
    }));
    string downloaded = await new PackageTransfer(client).DownloadAsync(package, cache, null, default);
    Check(await DistributionStorage.HashAsync(downloaded) == hash, "再開後の内容が一致しません");
    // 完了済みアーカイブを通信せず再利用する
    await new PackageTransfer(client).DownloadAsync(package, cache, null, default);
    Check(true, "キャッシュ再利用");
    using var badClient = new HttpClient(new Reply(_ => new(HttpStatusCode.OK) { Content = new ByteArrayContent(data) }));
    await Reject(() => new PackageTransfer(badClient).DownloadAsync(package with { Sha256 = new string('0', 64) }, cache, null, default), "SHA不一致を受理しました");
    await Reject(() => new PackageTransfer(badClient).DownloadAsync(package with { Url = "http://unit.invalid/test" }, cache, null, default), "HTTPを受理しました");
    string zipFile = Path.Combine(root, "slip.zip");
    using (var zip = ZipFile.Open(zipFile, ZipArchiveMode.Create)) {
        using var writer = new StreamWriter(zip.CreateEntry("../escape.txt").Open());
        writer.Write("x");
    }
    await Reject(() => PackageTransfer.ExtractAsync(zipFile, Path.Combine(root, "extract"), 1, null, default), "Zip Slipを受理しました");
    Check(!File.Exists(Path.Combine(root, "escape.txt")), "範囲外のファイルが作成されました");
    string validZip = Path.Combine(root, "valid.zip");
    using (var zip = ZipFile.Open(validZip, ZipArchiveMode.Create)) {
        using var writer = new StreamWriter(zip.CreateEntry("folder/data.txt").Open());
        writer.Write("abc");
    }
    await PackageTransfer.ExtractAsync(validZip, Path.Combine(root, "valid"), 3, null, default);
    Check(File.ReadAllText(Path.Combine(root, "valid/folder/data.txt")) == "abc", "展開内容が一致しません");

    string engine = Path.Combine(root, "engine");
    string sdk = Path.Combine(root, "sdk");
    Directory.CreateDirectory(sdk);
    File.WriteAllText(Path.Combine(sdk, "dotnet.exe"), "sdk");
    foreach (string file in new[] { "Editor/Develop/NEMEditor.exe", "Tools/ProjectTool/NEM.ProjectTool.dll" }) {
        string target = DistributionStorage.Child(engine, file);
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        File.WriteAllText(target, "fixture");
    }
    var manifest = new EngineManifest { Version = "0.1.0", BuildID = "test", ToolchainVersion = "10.0.401" };
    DistributionStorage.Write(Path.Combine(engine, "engine.json"), manifest);
    DistributionStorage.CopyTree(Path.Combine(repository, "Templates/ManagedProject"), Path.Combine(engine, "ProjectTemplates"));
    string templateScene = Path.Combine(engine, "ProjectTemplates/3D/GameAssets/Scenes/Main.scene.json");
    File.SetAttributes(templateScene, File.GetAttributes(templateScene) | FileAttributes.ReadOnly);
    var store = new LauncherStore(Path.Combine(root, "state"));
    store.Register(new InstalledEngine { Version = "0.1.0", Root = engine, ToolchainRoot = sdk });
    var projects = new ProjectService(store);
    var project = projects.Create("日本語 空白", Path.Combine(root, "projects"), "0.1.0", "3D");
    string projectRoot = Path.GetDirectoryName(project.Descriptor)!;
    Check(ProjectService.Read(project.Descriptor).EngineVersion == "0.1.0", "使用版を保存していません");
    Check(File.Exists(Path.Combine(projectRoot, "Scripts/GameScripts.csproj")), "C#プロジェクトがありません");
    Check(File.ReadAllText(Path.Combine(projectRoot, "GameAssets/Fonts/Charset/default_charset.txt")).Contains("0x7e"), "MSDFの文字集合がGameAssetsへコピーされません");
    Check((File.GetAttributes(Path.Combine(projectRoot, "GameAssets/Scenes/Main.scene.json")) & FileAttributes.ReadOnly) == 0,
        "共通テンプレートから読み取り専用属性を引き継いでいます");
    Check(!Directory.EnumerateFiles(projectRoot, "*.vcxproj", SearchOption.AllDirectories).Any(), "C++プロジェクトが生成されました");
    string descriptor = File.ReadAllText(project.Descriptor);
    Check(!descriptor.Contains(engine) && !descriptor.Contains(sdk), "PC固有のパスを共有設定へ保存しました");
    await Reject(() => Task.FromResult(projects.Create("CON", root, "0.1.0", "3D")), "予約名を受理しました");
    await Reject(() => Task.FromResult(projects.Create("日本語 空白", Path.Combine(root, "projects"), "0.1.0", "3D")), "既存プロジェクトを上書きしました");
    string lockFile = Path.Combine(projectRoot, "Library/EditorSession.lock");
    using (File.Open(lockFile, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.Read)) {
        Check(ProjectService.IsRunning(project.Descriptor), "二重起動のロックを検出しません");
        await Reject(() => { projects.ChangeVersion(project, "0.1.0"); return Task.CompletedTask; }, "使用中の版を変更しました");
    }
    Check(!ProjectService.IsRunning(project.Descriptor), "終了後のロックファイルを使用中と誤判定しました");
    using (File.Open(Path.Combine(engine, "engine.json"), FileMode.Open, FileAccess.Read, FileShare.Read))
        Check(EngineValidation.IsInUse(engine), "エンジン使用中を検出しません");
    Check(!EngineValidation.IsInUse(engine), "エンジンのロックを解放していません");
    await InstallationChecks.RunAsync(root, Check);
    if (args.Contains("--debugger") || args.Contains("--ide-context")) await VisualStudioDebuggerChecks.RunAsync(repository, root,
        args.FirstOrDefault(value => value.StartsWith("--release="))?[10..] ?? DistributionVersion.Current, args.Contains("--ide-context"), Check);
    if (args.Contains("--integration")) await DistributionIntegration.RunAsync(repository, root, Check,
        args.FirstOrDefault(value => value.StartsWith("--release="))?[10..] ?? DistributionVersion.Current);
    Console.WriteLine($"PASS {passed} distribution checks");
} catch (Exception error) {
    Console.Error.WriteLine(error);
    Environment.ExitCode = 1;
} finally {
    try { DistributionStorage.RemoveTree(root, owner); }
    catch (IOException error) { Console.Error.WriteLine("検証データを削除できません: " + error.Message); }
    catch (UnauthorizedAccessException error) { Console.Error.WriteLine("検証データを削除できません: " + error.Message); }
}

sealed class Reply(Func<HttpRequestMessage, HttpResponseMessage> response) : HttpMessageHandler {
    protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token) => Task.FromResult(response(request));
}
