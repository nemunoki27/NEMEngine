namespace NEMEngine.Distribution;

public sealed class EngineInstallService(LauncherStore store, HttpClient client) {
    private sealed record InstallationMarker(string Version, string Sha256, string ToolchainSha256 = "");

    public async Task<InstalledEngine> InstallAsync(EngineRelease release, bool repair,
        IProgress<TransferProgress>? progress, CancellationToken token) {
        DistributionStorage.RequireVersion(release.Version);
        DistributionStorage.RequireVersion(release.MinimumLauncherVersion);
        if (Version.Parse(release.MinimumLauncherVersion) > Version.Parse(DistributionVersion.Current))
            throw new InvalidOperationException("先にランチャーを更新してください");
        DistributionStorage.RequireID(release.Toolchain.ID);
        if (!repair && store.State.Engines.Any(engine => engine.Version == release.Version))
            throw new IOException("この版は既に登録されています 修復を選択してください");
        var existing = repair ? store.State.Engines.FirstOrDefault(engine => engine.Version == release.Version) : null;
        if (existing != null && !existing.Owned) throw new IOException("外部登録したエンジンは元の配布先で修復してください");
        string installRoot = Path.GetFullPath(existing?.OwnerRoot ?? store.State.InstallRoot);
        // MSBuildが条件付き読込を省略する長さを避ける
        string sdkTargets = Path.Combine(installRoot, "Toolchains", release.Toolchain.ID, "sdk", release.ToolchainVersion,
            "Sdks/Microsoft.NET.Sdk/targets/Microsoft.PackageDependencyResolution.targets");
        if (sdkTargets.Length >= 260) throw new IOException("インストール先のパスが長すぎます 短いフォルダーを選択してください");
        Directory.CreateDirectory(installRoot);
        // 複数の操作から同じインストール先を更新しない
        using var operation = new FileStream(Path.Combine(installRoot, ".installation.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        string engineRoot = DistributionStorage.Child(installRoot, "Editors/" + release.Version);
        string toolchainRoot = DistributionStorage.Child(installRoot, "Toolchains/" + release.Toolchain.ID);
        if (Directory.Exists(engineRoot)) {
            if (!repair) throw new IOException("この版は既にインストールされています");
            if (EngineValidation.IsInUse(engineRoot)) throw new IOException("使用中のEditorを保存して終了してから修復してください");
            var marker = DistributionStorage.Read<InstallationMarker>(Path.Combine(engineRoot, ".nem-installation.json"));
            if (marker.Version != release.Version || marker.Sha256 != release.Engine.Sha256 || marker.ToolchainSha256 != release.Toolchain.Sha256)
                throw new InvalidDataException("同じ版の配布内容が変更されています 上書きできません");
        }
        string cache = Path.Combine(store.Root, "Downloads");
        long needed = checked(release.Engine.Bytes + release.Engine.UnpackedBytes +
            (Directory.Exists(toolchainRoot) ? 0 : release.Toolchain.Bytes + release.Toolchain.UnpackedBytes));
        if (new DriveInfo(Path.GetPathRoot(installRoot)!).AvailableFreeSpace < needed)
            throw new IOException("インストール先の空き容量が足りません");
        var transfer = new PackageTransfer(client);
        await InstallToolchainAsync(release, toolchainRoot, cache, transfer, repair, progress, token);
        string archive = await transfer.DownloadAsync(release.Engine, cache, progress, token);
        string stage = DistributionStorage.Child(installRoot, ".engine-" + Guid.NewGuid().ToString("N"));
        string backup = stage + ".previous";
        try {
            await PackageTransfer.ExtractAsync(archive, stage, release.Engine.UnpackedBytes, progress, token);
            var manifest = EngineValidation.Read(stage);
            if (manifest.Version != release.Version || manifest.ToolchainVersion != release.ToolchainVersion)
                throw new InvalidDataException("エンジンとツールチェーンの組合せが一致しません");
            await EngineValidation.VerifyAsync(stage, manifest, progress, token);
            // 共有ファイルへの直接上書きも防ぐ
            foreach (var file in manifest.Files) {
                string path = DistributionStorage.Child(stage, file.Path);
                File.SetAttributes(path, File.GetAttributes(path) | FileAttributes.ReadOnly);
            }
            token.ThrowIfCancellationRequested();
            DistributionStorage.Write(Path.Combine(stage, ".nem-installation.json"), new InstallationMarker(release.Version, release.Engine.Sha256, release.Toolchain.Sha256));
            Directory.CreateDirectory(Path.GetDirectoryName(engineRoot)!);
            // 使用中になっていないか公開直前にも確認する
            using var engineLease = EngineValidation.AcquireForChange(engineRoot);
            if (Directory.Exists(engineRoot)) {
                if (EngineValidation.IsFileInUse(Path.Combine(engineRoot, EngineManifest.FileName))) throw new IOException("Editorが起動したため修復を中止しました");
                Directory.Move(engineRoot, backup);
            }
            try { Directory.Move(stage, engineRoot); }
            catch { if (Directory.Exists(backup)) Directory.Move(backup, engineRoot); throw; }
            var installed = new InstalledEngine { Version = release.Version, Root = engineRoot, ToolchainRoot = toolchainRoot, Owned = true, OwnerRoot = installRoot };
            try { store.Register(installed); }
            catch {
                Directory.Move(engineRoot, stage);
                if (Directory.Exists(backup)) Directory.Move(backup, engineRoot);
                throw;
            }
            DistributionStorage.RemoveTree(backup, installRoot);
            File.Delete(archive);
            return installed;
        } finally {
            DistributionStorage.RemoveTree(stage, installRoot);
        }
    }

    private static async Task InstallToolchainAsync(EngineRelease release, string destination, string cache,
        PackageTransfer transfer, bool repair, IProgress<TransferProgress>? progress, CancellationToken token) {
        if (Directory.Exists(destination)) {
            if (!repair) {
                var marker = DistributionStorage.Read<InstallationMarker>(Path.Combine(destination, ".nem-toolchain.json"));
                if (marker.Version != release.ToolchainVersion || marker.Sha256 != release.Toolchain.Sha256 || !File.Exists(Path.Combine(destination, "dotnet.exe")))
                    throw new InvalidDataException("共有ツールチェーンが不正です 修復してください");
                return;
            }
            if (EngineValidation.IsFileInUse(EngineValidation.LeasePath(destination)) || EngineValidation.IsFileInUse(Path.Combine(destination, "dotnet.exe")))
                throw new IOException("共有ツールチェーンが使用中です すべてのEditorとC#ビルドを終了してください");
        }
        string archive = await transfer.DownloadAsync(release.Toolchain, cache, progress, token);
        string parent = Path.GetDirectoryName(destination)!;
        Directory.CreateDirectory(parent);
        string stage = DistributionStorage.Child(parent, ".toolchain-" + Guid.NewGuid().ToString("N"));
        string backup = stage + ".previous";
        try {
            await PackageTransfer.ExtractAsync(archive, stage, release.Toolchain.UnpackedBytes, progress, token);
            if (!File.Exists(Path.Combine(stage, "dotnet.exe")) || !Directory.Exists(Path.Combine(stage, "sdk", release.ToolchainVersion)))
                throw new InvalidDataException("指定版の.NET SDKが含まれていません");
            DistributionStorage.Write(Path.Combine(stage, ".nem-toolchain.json"), new InstallationMarker(release.ToolchainVersion, release.Toolchain.Sha256));
            token.ThrowIfCancellationRequested();
            using var toolchainLease = EngineValidation.AcquireForChange(destination);
            if (EngineValidation.IsFileInUse(Path.Combine(destination, "dotnet.exe"))) throw new IOException("C#ビルドが開始されたため修復を中止しました");
            if (Directory.Exists(destination)) Directory.Move(destination, backup);
            try { Directory.Move(stage, destination); }
            catch { if (Directory.Exists(backup)) Directory.Move(backup, destination); throw; }
            DistributionStorage.RemoveTree(backup, parent);
            File.Delete(archive);
        } finally { DistributionStorage.RemoveTree(stage, parent); }
    }

    public void Uninstall(InstalledEngine engine) {
        if (!engine.Owned) throw new InvalidOperationException("登録したフォルダーは一覧から解除してください");
        string expectedRoot = DistributionStorage.Child(engine.OwnerRoot, "Editors/" + engine.Version);
        if (!Path.GetFullPath(engine.Root).Equals(expectedRoot, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("エンジンの削除先が登録された配置と一致しません");
        if (Directory.Exists(engine.Root)) {
            var marker = DistributionStorage.Read<InstallationMarker>(Path.Combine(engine.Root, ".nem-installation.json"));
            if (marker.Version != engine.Version) throw new InvalidDataException("インストール先の識別情報が一致しません");
        }
        if (EngineValidation.IsInUse(engine.Root)) throw new IOException("使用中のEditorを終了してください");
        using var engineLease = EngineValidation.AcquireForChange(engine.Root);
        DistributionStorage.RemoveTree(engine.Root, engine.OwnerRoot);
        store.RemoveEngine(engine.Version);
        // 他の版が参照するツールチェーンを残す
        if (store.State.Engines.All(item => !item.ToolchainRoot.Equals(engine.ToolchainRoot, StringComparison.OrdinalIgnoreCase)) &&
            File.Exists(Path.Combine(engine.ToolchainRoot, ".nem-toolchain.json")) &&
            DistributionStorage.IsInside(engine.ToolchainRoot, engine.OwnerRoot) &&
            !EngineValidation.IsFileInUse(Path.Combine(engine.ToolchainRoot, "dotnet.exe"))) {
            try {
                using var toolchainLease = EngineValidation.AcquireForChange(engine.ToolchainRoot);
                DistributionStorage.RemoveTree(engine.ToolchainRoot, engine.OwnerRoot);
            } catch (IOException) { }
        }
    }
}
