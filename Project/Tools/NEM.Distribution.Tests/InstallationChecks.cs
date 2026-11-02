using System.IO.Compression;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using NEMEngine.Distribution;

internal static class InstallationChecks {
    public static async Task RunAsync(string root, Action<bool, string> check) {
        string directory = Path.Combine(root, "installation");
        var store = new LauncherStore(Path.Combine(directory, "state"));
        store.State.InstallRoot = Path.Combine(directory, "install");
        store.SaveSettings();
        var files = new Dictionary<string, byte[]>();
        foreach (string path in new[] { "Editor/Develop/NEMEditor.exe", "Tools/ProjectTool/NEM.ProjectTool.dll",
            "Runtime/Release/NEMPlayer.exe", "Runtime/Release/NEMRuntime.dll", "Managed/Ref/NEM.ScriptCore.dll", "Tools/NEMBuildTool/NEMBuildTool.exe" })
            files[path] = Encoding.UTF8.GetBytes("fixture:" + path);
        var manifest = new EngineManifest { Version = "0.1.0", ToolchainVersion = "10.0.401", BuildID = "test",
            Files = files.Select(entry => new EngineFile { Path = entry.Key, Bytes = entry.Value.Length,
                Sha256 = Convert.ToHexString(SHA256.HashData(entry.Value)).ToLowerInvariant() }).ToList() };
        files["engine.json"] = JsonSerializer.SerializeToUtf8Bytes(manifest, DistributionStorage.JsonOptions);
        var sdkFiles = new Dictionary<string, byte[]> { ["dotnet.exe"] = [1, 2, 3], ["sdk/10.0.401/fixture.txt"] = [4] };
        byte[] engineZip = Archive(files), sdkZip = Archive(sdkFiles);
        var release = new EngineRelease { Version = "0.1.0", ToolchainVersion = "10.0.401",
            Engine = Package("engine", engineZip, files.Values.Sum(bytes => bytes.Length)),
            Toolchain = Package("sdk", sdkZip, sdkFiles.Values.Sum(bytes => bytes.Length)) };
        using var client = new HttpClient(new Packages(new Dictionary<string, byte[]> { ["/engine"] = engineZip, ["/sdk"] = sdkZip }));
        var service = new EngineInstallService(store, client);
        var engine = await service.InstallAsync(release, false, null, default);
        check(Directory.Exists(engine.Root) && store.State.Engines.Count == 1, "インストールを公開できません");
        string executable = Path.Combine(engine.Root, manifest.Editor);
        check((File.GetAttributes(executable) & FileAttributes.ReadOnly) != 0, "共有ファイルが読み取り専用ではありません");
        File.SetAttributes(executable, FileAttributes.Normal);
        File.WriteAllText(executable, "broken");
        await service.InstallAsync(release, true, null, default);
        check(File.ReadAllBytes(executable).SequenceEqual(files[manifest.Editor]), "修復後の内容が一致しません");
        File.Delete(Path.Combine(engine.ToolchainRoot, "dotnet.exe"));
        await service.InstallAsync(release, true, null, default);
        check(File.Exists(Path.Combine(engine.ToolchainRoot, "dotnet.exe")), "共有SDKを修復できません");
        using (File.Open(Path.Combine(engine.Root, "engine.json"), FileMode.Open, FileAccess.Read, FileShare.Read)) {
            try { service.Uninstall(engine); throw new Exception("使用中の版を削除しました"); }
            catch (IOException) { check(Directory.Exists(engine.Root), "使用中の版を破壊しました"); }
        }
        using (File.Open(Path.Combine(engine.ToolchainRoot, "dotnet.exe"), FileMode.Open, FileAccess.Read, FileShare.Read)) {
            try { await service.InstallAsync(release, true, null, default); throw new Exception("使用中のSDKを置換しました"); }
            catch (IOException) { check(File.Exists(executable), "修復の中止で旧版を失いました"); }
        }
        // 別の版の失敗でインストール済みの版を壊さない
        try { await service.InstallAsync(release with { Version = "0.2.0" }, false, null, default); throw new Exception("版の異なる配布物を受理しました"); }
        catch (InvalidDataException) { check(File.Exists(executable) && store.State.Engines.Count == 1, "失敗した版を登録しました"); }
        service.Uninstall(engine);
        check(!Directory.Exists(engine.Root) && store.State.Engines.Count == 0, "アンインストールできません");
        check(!Directory.Exists(engine.ToolchainRoot), "未使用の共有SDKが残っています");
    }

    private static byte[] Archive(Dictionary<string, byte[]> files) {
        using var stream = new MemoryStream();
        using (var zip = new ZipArchive(stream, ZipArchiveMode.Create, true)) {
            foreach (var file in files) {
                using var output = zip.CreateEntry(file.Key).Open();
                output.Write(file.Value);
            }
        }
        return stream.ToArray();
    }

    private static PackageInfo Package(string id, byte[] bytes, long unpacked) => new() {
        ID = id, Url = "https://unit.invalid/" + id, Bytes = bytes.Length, UnpackedBytes = unpacked,
        Sha256 = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant()
    };

    private sealed class Packages(Dictionary<string, byte[]> data) : HttpMessageHandler {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token) {
            return Task.FromResult(new HttpResponseMessage(HttpStatusCode.OK) { Content = new ByteArrayContent(data[request.RequestUri!.AbsolutePath]) });
        }
    }
}
