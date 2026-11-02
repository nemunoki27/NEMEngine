namespace NEMEngine.Distribution;

// GitHubの公開操作とローカルの適用順を管理する
public sealed partial class GitProjectService(LauncherStore store, GitToolchainService tools, GitHubSession session,
    IGitHubRepositories repositories, string launcher) {
    public GitProjectService ForAccount(long accountID) => new(store, tools, session.ForAccount(accountID), repositories.ForAccount(accountID), launcher);

    public GitProjectBinding? ReadBinding(ProjectEntry project) => GitProjectReset.Read(store.Root, Path.GetDirectoryName(project.Descriptor)!)?.Binding ??
        GitProjectBinding.Read(Path.GetDirectoryName(project.Descriptor)!);

    public async Task<GitUploadPreview> PreviewAsync(ProjectEntry project, IProgress<TransferProgress>? progress, CancellationToken token) {
        string root = await ProjectRootAsync(project, token);
        await using var operation = await OperationFileLock.AcquireAsync(store.Root, "project:" + root.ToUpperInvariant(), token);
        var runner = await RunnerAsync(progress, token);
        progress?.Report(new("共有するファイルを確認中"));
        return await new GitWorktree(runner).PreviewAsync(root, token);
    }

    public async Task<GitProjectBinding?> RecognizeAsync(ProjectEntry project, IProgress<TransferProgress>? progress, CancellationToken token) {
        string root = await ProjectRootAsync(project, token);
        var binding = ReadBinding(project);
        if (binding?.Repository != null) return binding;
        if (!Directory.Exists(Path.Combine(root, ".git"))) return null;
        var runner = await RunnerAsync(progress, token);
        await new GitWorktree(runner).RequireRootAsync(root, token);
        var origin = await runner.RunAsync(root, ["remote", "get-url", "origin"], token, allowFailure: true);
        if (origin.ExitCode != 0 || origin.Output.Trim().Length == 0) return null;
        string fullName = GitHubRepositories.ParseURL(origin.Output.Trim());
        var identity = RequireIdentity();
        var repository = await repositories.ForAccount(identity.ID).GetAsync(fullName, token);
        binding = new() { Repository = repository, AccountID = identity.ID, UploadComplete = true };
        binding.Save(root);
        return binding;
    }

    public async Task CreateAsync(ProjectEntry project, GitHubOwner owner, string name, string description, bool isPrivate,
        IProgress<TransferProgress>? progress, CancellationToken token) {
        if (!session.IsAccountBound) { await ForAccount(RequireIdentity().ID).CreateAsync(project, owner, name, description, isPrivate, progress, token); return; }
        string root = await ProjectRootAsync(project, token);
        await using var operation = await OperationFileLock.AcquireAsync(store.Root, "project:" + root.ToUpperInvariant(), token);
        var identity = RequireIdentity();
        var runner = await RunnerAsync(progress, token);
        var worktree = new GitWorktree(runner);
        await worktree.PreviewAsync(root, token);
        var existing = GitProjectBinding.Read(root);
        if (existing?.Repository != null) throw new IOException("リポジトリは作成済みです 初回送信の再開を選択してください");
        var origin = await runner.RunAsync(root, ["remote", "get-url", "origin"], token, allowFailure: true);
        if (origin.ExitCode == 0 && origin.Output.Trim().Length != 0) throw new IOException("既存のoriginがあります 接続先は上書きしません");
        progress?.Report(new("GitHubリポジトリを作成中"));
        var repository = await repositories.CreateAsync(owner, name, description, isPrivate, token);
        // 通信後はキャンセルよりも作成結果の保存を先に行う
        var binding = new GitProjectBinding { Repository = repository, AccountID = identity.ID };
        binding.Save(root);
        binding = await worktree.ConfigureCredentialsAsync(root, binding, token);
        binding.Save(root);
        await worktree.CommitAndPushAsync(root, binding, identity, progress, token);
        (binding with { UploadComplete = true }).Save(root);
    }

    public async Task ResumeUploadAsync(ProjectEntry project, IProgress<TransferProgress>? progress, CancellationToken token) {
        string root = await ProjectRootAsync(project, token);
        await using var operation = await OperationFileLock.AcquireAsync(store.Root, "project:" + root.ToUpperInvariant(), token);
        var binding = GitProjectBinding.Read(root) ?? throw new IOException("初回送信の記録がありません");
        if (binding.Repository == null || binding.RemoteDeleted || binding.UploadComplete) throw new IOException("初回送信を再開できる状態ではありません");
        var identity = session.FindAccount(binding.AccountID) ?? throw new IOException("作成したGitHubアカウントを追加してください");
        var actual = await repositories.ForAccount(identity.ID).GetAsync(binding.Repository.FullName, token);
        if (actual.ID != binding.Repository.ID) throw new IOException("リポジトリの識別情報が一致しません");
        var worktree = new GitWorktree(await RunnerAsync(progress, token));
        binding = await worktree.ConfigureCredentialsAsync(root, binding, token);
        binding.Save(root);
        await worktree.CommitAndPushAsync(root, binding, identity, progress, token);
        (binding with { UploadComplete = true }).Save(root);
    }

    public async Task ChangeAccountAsync(ProjectEntry project, long accountID, IProgress<TransferProgress>? progress, CancellationToken token) {
        string root = await ProjectRootAsync(project, token);
        await using var operation = await OperationFileLock.AcquireAsync(store.Root, "project:" + root.ToUpperInvariant(), token);
        if (GitProjectReset.Read(store.Root, root) != null) throw new IOException("Git設定の初期化を完了してから変更してください");
        var previous = GitProjectBinding.Read(root) ?? throw new IOException("GitHubの接続情報がありません");
        if (previous.Repository == null || previous.RemoteDeleted) throw new IOException("GitHubの接続先がありません");
        _ = session.FindAccount(accountID) ?? throw new IOException("使用するGitHubアカウントを追加してください");
        var actual = await repositories.ForAccount(accountID).GetAsync(previous.Repository.FullName, token);
        if (actual.ID != previous.Repository.ID || !actual.Push) throw new IOException("このアカウントではリポジトリへ送信できません");
        var runner = await RunnerAsync(progress, token);
        var worktree = new GitWorktree(runner);
        await worktree.RequireRootAsync(root, token);
        string config = Path.Combine(root, ".git/config");
        byte[] original = File.ReadAllBytes(config);
        try {
            var binding = await worktree.ConfigureCredentialsAsync(root, previous with { AccountID = accountID, Repository = actual }, token);
            binding.Save(root);
        } catch (Exception failure) {
            try { File.WriteAllBytes(config, original); previous.Save(root); }
            catch (Exception recovery) { throw new AggregateException("Gitの認証設定を復元できません", failure, recovery); }
            throw;
        }
    }

    private GitHubIdentity RequireIdentity() => session.Identity ?? throw new InvalidOperationException("GitHubへ接続してください");
    private Task<string> ProjectRootAsync(ProjectEntry project, CancellationToken token) =>
        Task.Run(() => new ProjectService(store).GetDeletionDirectory(project, token), token);
    private async Task<GitCommandRunner> RunnerAsync(IProgress<TransferProgress>? progress, CancellationToken token) =>
        new(await tools.EnsureAsync(progress, token), launcher, tools.Root, session.Root);
}
