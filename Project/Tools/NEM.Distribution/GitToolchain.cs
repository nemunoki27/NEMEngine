namespace NEMEngine.Distribution;

public sealed record GitToolsManifest {
    public string ID { get; init; } = "";
    public string GitVersion { get; init; } = "";
    public string LfsVersion { get; init; } = "";
    public PackageInfo Git { get; init; } = new();
    public PackageInfo Lfs { get; init; } = new();
    public string GitExecutable { get; init; } = "cmd/git.exe";
    public string LfsExecutable { get; init; } = "git-lfs.exe";
}

public sealed record GitToolchain(string Git, string LFS) {
    public string PathPrefix => Path.GetDirectoryName(Git) + Path.PathSeparator.ToString() + Path.GetDirectoryName(LFS);
}

// Git本体とLFSを専用フォルダーへ準備する
public sealed class GitToolchainService(string root, HttpClient client, GitToolsManifest? configuration = null) {
    public string Root => root;
    private readonly GitToolsManifest manifest_ = configuration ?? DistributionDefaults.GitTools;

    public GitToolchain? FindInstalled() {
        string destination = DistributionStorage.Child(Path.Combine(root, "Tools"), manifest_.ID);
        string marker = Path.Combine(destination, "installation.json");
        if (!File.Exists(marker)) return null;
        var installed = DistributionStorage.Read<GitToolsManifest>(marker);
        if (installed.ID != manifest_.ID || installed.Git.Sha256 != manifest_.Git.Sha256 || installed.Lfs.Sha256 != manifest_.Lfs.Sha256)
            return null;
        var tools = Paths(destination);
        return File.Exists(tools.Git) && File.Exists(tools.LFS) ? tools : null;
    }

    public async Task<GitToolchain> EnsureAsync(IProgress<TransferProgress>? progress, CancellationToken token) {
        DistributionStorage.RequireID(manifest_.ID);
        await using var operation = await OperationFileLock.AcquireAsync(root, "git-tools", token);
        if (FindInstalled() is { } installed) return installed;
        string toolsRoot = Path.Combine(root, "Tools");
        string destination = DistributionStorage.Child(toolsRoot, manifest_.ID);
        string stage = DistributionStorage.Child(toolsRoot, ".git-tools-" + Guid.NewGuid().ToString("N"));
        var transfer = new PackageTransfer(client);
        try {
            string git = await transfer.DownloadAsync(manifest_.Git, Path.Combine(root, "Downloads"), progress, token);
            string lfs = await transfer.DownloadAsync(manifest_.Lfs, Path.Combine(root, "Downloads"), progress, token);
            await PackageTransfer.ExtractAsync(git, Path.Combine(stage, "Git"), manifest_.Git.UnpackedBytes, progress, token);
            await PackageTransfer.ExtractAsync(lfs, Path.Combine(stage, "LFS"), manifest_.Lfs.UnpackedBytes, progress, token);
            if (!File.Exists(Paths(stage).Git) || !File.Exists(Paths(stage).LFS)) throw new IOException("Gitツールの実行ファイルがありません");
            DistributionStorage.Write(Path.Combine(stage, "installation.json"), manifest_);
            token.ThrowIfCancellationRequested();
            if (Directory.Exists(destination)) {
                if (!File.Exists(Path.Combine(destination, "installation.json"))) throw new IOException("所有を確認できないGitツールの配置は変更できません");
                DistributionStorage.RemoveTree(destination, toolsRoot);
            }
            Directory.Move(stage, destination);
            File.Delete(git);
            File.Delete(lfs);
            return Paths(destination);
        } finally { DistributionStorage.RemoveTree(stage, toolsRoot); }
    }

    private GitToolchain Paths(string destination) => new(
        DistributionStorage.Child(Path.Combine(destination, "Git"), manifest_.GitExecutable),
        DistributionStorage.Child(Path.Combine(destination, "LFS"), manifest_.LfsExecutable));
}
