namespace NEMEngine.Distribution;

public sealed record GitProjectBinding {
    public GitHubRepository? Repository { get; init; }
    public long AccountID { get; init; }
    public bool UploadComplete { get; init; }
    public bool RemoteDeleted { get; init; }
    public string CredentialHelper { get; init; } = "";
    public string[] PreviousHelpers { get; init; } = [];

    public static GitProjectBinding? Read(string root) {
        string file = Path.Combine(root, ".git", "nemlauncher.json");
        return File.Exists(file) ? DistributionStorage.Read<GitProjectBinding>(file) : null;
    }

    public void Save(string root) {
        if (!Directory.Exists(Path.Combine(root, ".git"))) throw new IOException("通常のGitリポジトリではありません");
        DistributionStorage.Write(Path.Combine(root, ".git", "nemlauncher.json"), this);
    }
}

public sealed record GitUploadFile(string Path, long Bytes, bool LFS, bool Deleted);
public sealed record GitUploadPreview(string Root, IReadOnlyList<GitUploadFile> Files, string Branch) {
    public long Bytes => Files.Sum(file => file.Bytes);
}
