using System.Text.Json.Nodes;

namespace NEMEngine.Distribution;

public sealed partial class GitProjectService {
    public async Task DeleteRemoteAsync(ProjectEntry project, string confirmation, IProgress<TransferProgress>? progress, CancellationToken token) {
        string root = await ProjectRootAsync(project, token);
        await using var operation = await OperationFileLock.AcquireAsync(store.Root, "project:" + root.ToUpperInvariant(), token);
        var pending = GitProjectReset.Read(store.Root, root);
        var binding = pending?.Binding ?? GitProjectBinding.Read(root) ?? throw new IOException("GitHubの接続情報がありません");
        var repository = binding.Repository ?? throw new IOException("GitHubの接続先がありません");
        if (!confirmation.Equals(repository.FullName, StringComparison.Ordinal)) throw new ArgumentException("所有者/リポジトリ名をそのまま入力してください");
        if (OperatingSystem.IsWindows() && VisualStudioInstances.IsOpen(Path.ChangeExtension(project.Descriptor, ".slnx")))
            throw new IOException("Visual StudioのSolutionを閉じてからGitリポジトリを削除してください");
        ValidateResetRoot(root);
        if (!binding.RemoteDeleted) {
            var runner = await RunnerAsync(progress, token);
            await new GitWorktree(runner).RequireRootAsync(root, token);
            var origin = await runner.RunAsync(root, ["remote", "get-url", "origin"], token, allowFailure: true);
            if (origin.ExitCode == 0 && !GitHubRepositories.ParseURL(origin.Output.Trim()).Equals(repository.FullName, StringComparison.OrdinalIgnoreCase))
                throw new IOException("originが別の接続先へ変わっています 削除を中止しました");
            await MaterializeLFSAsync(root, binding, runner, progress, token);
            token.ThrowIfCancellationRequested();
            pending = new(root, binding);
            pending.Save(store.Root);
            progress?.Report(new("GitHub上のリポジトリを削除中"));
            await repositories.ForAccount(binding.AccountID).DeleteAsync(repository, token);
            // 遠隔削除の完了を.gitの外へ残す
            binding = binding with { RemoteDeleted = true };
            pending = pending with { Binding = binding };
            pending.Save(store.Root);
        }
        pending ??= new(root, binding);
        pending.Save(store.Root);
        progress?.Report(new("ローカルGit設定を初期化中"));
        await Task.Run(() => GitProjectReset.Restore(root), CancellationToken.None);
        pending.Complete(store.Root);
    }

    private static void ValidateResetRoot(string root) {
        foreach (string name in new[] { "index.lock", "config.lock" })
            if (File.Exists(Path.Combine(root, ".git", name))) throw new IOException("Gitの処理が使用中です 終了してから再実行してください");
        var directories = new Stack<string>();
        directories.Push(root);
        while (directories.TryPop(out string? current)) {
            foreach (string entry in Directory.EnumerateFileSystemEntries(current)) {
                if (Path.GetFileName(entry).Equals(".git", StringComparison.OrdinalIgnoreCase)) {
                    if (current != root || !Directory.Exists(entry)) throw new IOException("worktreeやsubmodule、入れ子のGitリポジトリは初期化できません");
                    continue;
                }
                if (Directory.Exists(entry)) directories.Push(entry);
            }
        }
    }

    private static bool IsLFSPointer(string file) {
        if (!File.Exists(file) || new FileInfo(file).Length > 1024) return false;
        string text = File.ReadAllText(file);
        return text.StartsWith("version https://git-lfs.github.com/spec/v1\n", StringComparison.Ordinal) ||
            text.StartsWith("version https://git-lfs.github.com/spec/v1\r\n", StringComparison.Ordinal);
    }

    private static async Task MaterializeLFSAsync(string root, GitProjectBinding binding, GitCommandRunner runner,
        IProgress<TransferProgress>? progress, CancellationToken token) {
        var listing = await runner.RunAsync(root, ["lfs", "ls-files", "--json"], token);
        var files = (JsonNode.Parse(listing.Output)?["files"]?.AsArray() ?? new JsonArray()).Where(item => item?["name"] != null)
            .Select(item => item!["name"]!.GetValue<string>())
            .Where(name => IsLFSPointer(DistributionStorage.Child(root, name))).ToArray();
        if (files.Length == 0) return;
        progress?.Report(new("LFSの実データを確認中"));
        foreach (var batch in files.Chunk(32)) {
            // 実在するポインターだけ展開し、削除済みファイルを復元しない
            await runner.RunAsync(root, new[] { "lfs", "checkout", "--" }.Concat(batch), token);
            var missing = batch.Where(name => IsLFSPointer(DistributionStorage.Child(root, name))).ToArray();
            if (missing.Length != 0) {
                await runner.RunAsync(root, ["-c", "lfs.fetchrecentalways=false", "lfs", "fetch", "--include=", "--exclude=", "origin", "HEAD"], token,
                    fullName: binding.Repository!.FullName, accountID: binding.AccountID);
                await runner.RunAsync(root, new[] { "lfs", "checkout", "--" }.Concat(missing), token);
            }
        }
        if (files.Any(name => IsLFSPointer(DistributionStorage.Child(root, name))))
            throw new IOException("LFSの実データを取得できません Gitリポジトリの削除を中止しました");
    }
}
