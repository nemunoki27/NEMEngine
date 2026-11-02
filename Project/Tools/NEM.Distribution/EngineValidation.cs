namespace NEMEngine.Distribution;

public static class EngineValidation {
    public static EngineManifest Read(string root) {
        var manifest = DistributionStorage.Read<EngineManifest>(Path.Combine(root, EngineManifest.FileName));
        DistributionStorage.RequireVersion(manifest.Version);
        if (manifest.SchemaVersion != 1 || manifest.Platform != "win-x64" || string.IsNullOrWhiteSpace(manifest.BuildID))
            throw new InvalidDataException("対応するエンジンの配布情報ではありません");
        if (!File.Exists(DistributionStorage.Child(root, manifest.Editor)) || !File.Exists(DistributionStorage.Child(root, manifest.ProjectTool)))
            throw new InvalidDataException("Editorまたはプロジェクトツールがありません 修復してください");
        return manifest;
    }

    public static async Task VerifyAsync(string root, EngineManifest manifest, IProgress<TransferProgress>? progress, CancellationToken token) {
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        if (manifest.Files.Count == 0) throw new InvalidDataException("エンジンのファイル一覧がありません");
        for (int index = 0; index < manifest.Files.Count; ++index) {
            token.ThrowIfCancellationRequested();
            var entry = manifest.Files[index];
            string file = DistributionStorage.Child(root, entry.Path);
            if (!paths.Add(file) || !File.Exists(file) || new FileInfo(file).Length != entry.Bytes ||
                await DistributionStorage.HashAsync(file, token) != entry.Sha256.ToLowerInvariant())
                throw new InvalidDataException("エンジンのファイルが一致しません: " + entry.Path);
            progress?.Report(new("エンジンを検証中", index + 1, manifest.Files.Count));
        }
        foreach (string required in new[] { manifest.Editor, manifest.ProjectTool, "Runtime/Release/NEMPlayer.exe",
            "Runtime/Release/NEMRuntime.dll", "Managed/Ref/NEM.ScriptCore.dll", "Tools/NEMBuildTool/NEMBuildTool.exe" }) {
            if (!paths.Contains(DistributionStorage.Child(root, required)))
                throw new InvalidDataException("配布一覧に必須ファイルがありません: " + required);
        }
    }

    public static bool IsInUse(string root) {
        return IsFileInUse(LeasePath(root)) || IsFileInUse(Path.Combine(root, EngineManifest.FileName));
    }

    public static bool IsFileInUse(string path) {
        if (!File.Exists(path)) return false;
        try {
            using var lease = File.Open(path, FileMode.Open, FileAccess.Read, FileShare.None);
            return false;
        } catch (IOException) { return true; }
    }

    public static string LeasePath(string root) {
        string normalized = Path.TrimEndingDirectorySeparator(Path.GetFullPath(root));
        return Path.Combine(Path.GetDirectoryName(normalized)!, "." + Path.GetFileName(normalized) + ".lease");
    }

    // 置換対象の外側で起動と変更を排他する
    public static FileStream AcquireForChange(string root) {
        string path = LeasePath(root);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        return new FileStream(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
    }
}
