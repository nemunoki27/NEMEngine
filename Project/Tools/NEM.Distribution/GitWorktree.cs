namespace NEMEngine.Distribution;

// ローカルのGit履歴とLFS設定を扱う
public sealed class GitWorktree(GitCommandRunner runner) {
    public async Task EnsureAsync(string root, CancellationToken token) {
        var found = await runner.RunAsync(root, ["rev-parse", "--show-toplevel"], token, allowFailure: true);
        if (found.ExitCode == 0 && !Path.GetFullPath(found.Output.Trim()).Equals(Path.GetFullPath(root), StringComparison.OrdinalIgnoreCase))
            throw new IOException("親フォルダーのGit管理下です このプロジェクト専用のリポジトリを使用してください");
        if (File.Exists(Path.Combine(root, ".git"))) throw new IOException("worktreeやsubmoduleのGit設定は変更できません");
        if (found.ExitCode != 0) await runner.RunAsync(root, ["init", "--initial-branch=main", "--", root], token);
        GitProjectAttributes.MergeTemplate(root, ".gitignore");
        GitProjectAttributes.MergeTemplate(root, ".gitattributes");
        await ConfigureLFSAsync(root, token);
    }

    public async Task RequireRootAsync(string root, CancellationToken token) {
        var result = await runner.RunAsync(root, ["rev-parse", "--show-toplevel"], token);
        if (File.Exists(Path.Combine(root, ".git")) || !Path.GetFullPath(result.Output.Trim()).Equals(Path.GetFullPath(root), StringComparison.OrdinalIgnoreCase))
            throw new IOException("このプロジェクト専用の通常のGitリポジトリを使用してください");
    }

    public async Task<GitUploadPreview> PreviewAsync(string root, CancellationToken token) {
        await EnsureAsync(root, token);
        string branch = (await runner.RunAsync(root, ["symbolic-ref", "--short", "HEAD"], token)).Output.Trim();
        var listing = await runner.RunAsync(root, ["ls-files", "--cached", "--others", "--exclude-standard", "-z"], token);
        var files = listing.Output.Split('\0', StringSplitOptions.RemoveEmptyEntries).Distinct(StringComparer.Ordinal).Select(path => {
            string full = DistributionStorage.Child(root, path);
            long bytes = File.Exists(full) ? new FileInfo(full).Length : 0;
            return new GitUploadFile(path, bytes, GitProjectAttributes.UsesLFS(path, bytes), !File.Exists(full));
        }).ToArray();
        foreach (var file in files) {
            string top = file.Path.Replace('\\', '/').Split('/')[0];
            if (top is "Library" or "Managed" or "Saved" or "Generated" or "UserSettings" or ".vs")
                throw new IOException("生成物が既に追跡されています Visual Studioで追跡を解除してください: " + file.Path);
        }
        GitProjectAttributes.Update(root, files);
        if (files.Length != 0) {
            var attributes = await runner.RunAsync(root, ["check-attr", "--stdin", "-z", "filter"], token,
                input: string.Join('\0', files.Select(file => file.Path)) + '\0');
            string[] fields = attributes.Output.Split('\0', StringSplitOptions.RemoveEmptyEntries);
            var lfs = new HashSet<string>(StringComparer.Ordinal);
            for (int index = 0; index + 2 < fields.Length; index += 3)
                if (fields[index + 2] == "lfs") lfs.Add(fields[index]);
            files = files.Select(file => file with { LFS = lfs.Contains(file.Path) }).ToArray();
        }
        return new(root, files, branch);
    }

    public async Task ConfigureLFSAsync(string root, CancellationToken token) {
        var hooks = await runner.RunAsync(root, ["config", "--get", "core.hooksPath"], token, allowFailure: true);
        if (hooks.ExitCode == 0 && hooks.Output.Trim().Length != 0) throw new IOException("既存のhooksPathが設定されています Git LFSのpre-push設定を確認してください");
        foreach (var value in new[] { ("clean", "clean -- %f"), ("smudge", "smudge -- %f"), ("process", "filter-process") })
            await runner.RunAsync(root, ["config", "--local", "filter.lfs." + value.Item1, runner.LfsCommand + " " + value.Item2], token);
        await runner.RunAsync(root, ["config", "--local", "filter.lfs.required", "true"], token);
        string path = Path.Combine(root, ".git/hooks/pre-push");
        if (File.Exists(path)) {
            string existing = File.ReadAllText(path);
            string[] lines = existing.Split('\n', StringSplitOptions.RemoveEmptyEntries).Select(line => line.Trim()).Where(line => line.Length != 0).ToArray();
            bool standard = lines.Length == 3 && lines[0] == "#!/bin/sh" && lines[1].StartsWith("command -v git-lfs ", StringComparison.Ordinal) &&
                lines[2] is "git lfs pre-push \"$@\"" or "git-lfs pre-push \"$@\"";
            if (!existing.Contains("# NEMLauncher LFS", StringComparison.Ordinal) && !standard)
                throw new IOException("既存のpre-pushを上書きできません Git LFSとの接続を確認してください");
        }
        DistributionStorage.WriteText(path, "#!/bin/sh\n# NEMLauncher LFS\nexec " + runner.LfsCommand + " pre-push \"$@\"\n");
    }

    public async Task<GitProjectBinding> ConfigureCredentialsAsync(string root, GitProjectBinding binding, CancellationToken token) {
        string helper = runner.CredentialCommand(binding.Repository!.FullName, binding.AccountID);
        if (binding.CredentialHelper != helper) {
            var previous = await runner.RunAsync(root, ["config", "--local", "--get-all", "credential.https://github.com.helper"], token, allowFailure: true);
            binding = binding with { CredentialHelper = helper, PreviousHelpers = binding.CredentialHelper.Length != 0 ? binding.PreviousHelpers :
                previous.ExitCode == 0 ? previous.Output.TrimEnd('\r', '\n').Split('\n') : [] };
            // 中断されても元の認証設定を再開時に失わない
            binding.Save(root);
        }
        await runner.RunAsync(root, ["config", "--local", "--replace-all", "credential.https://github.com.helper", ""], token);
        await runner.RunAsync(root, ["config", "--local", "--add", "credential.https://github.com.helper", helper], token);
        await runner.RunAsync(root, ["config", "--local", "credential.https://github.com.useHttpPath", "true"], token);
        return binding;
    }

    public async Task CommitAndPushAsync(string root, GitProjectBinding binding, GitHubIdentity identity, IProgress<TransferProgress>? progress, CancellationToken token) {
        string fullName = binding.Repository!.FullName;
        progress?.Report(new("送信ファイルを準備中"));
        await PreviewAsync(root, token);
        string origin = (await runner.RunAsync(root, ["remote", "get-url", "origin"], token, allowFailure: true)).Output.Trim();
        if (origin.Length == 0) await runner.RunAsync(root, ["remote", "add", "origin", binding.Repository.CloneURL], token);
        else if (!GitHubRepositories.ParseURL(origin).Equals(fullName, StringComparison.OrdinalIgnoreCase))
            throw new IOException("既存のoriginが別のリポジトリを参照しています");
        foreach (var value in new[] { ("name", identity.Name.Length == 0 ? identity.Login : identity.Name), ("email", identity.CommitEmail) }) {
            var configured = await runner.RunAsync(root, ["config", "--get", "user." + value.Item1], token, allowFailure: true);
            if (configured.ExitCode != 0 || configured.Output.Trim().Length == 0)
                await runner.RunAsync(root, ["config", "--local", "user." + value.Item1, value.Item2], token);
        }
        await runner.RunAsync(root, ["add", "--all", "--", "."], token);
        var changed = await runner.RunAsync(root, ["diff", "--cached", "--quiet"], token, allowFailure: true);
        if (changed.ExitCode == 1) await runner.RunAsync(root, ["commit", "-m", "プロジェクトを作成"], token);
        else if (changed.ExitCode != 0) throw new IOException("commit対象を確認できません");
        // 過去の大容量blobを履歴書換えで隠さない
        var objects = await runner.RunAsync(root, ["rev-list", "--objects", "--all"], token);
        var sizes = await runner.RunAsync(root, ["cat-file", "--batch-check=%(objecttype) %(objectsize) %(rest)"], token, input: objects.Output);
        foreach (string line in sizes.Output.Split('\n')) {
            var fields = line.Split(' ', 3);
            if (fields.Length >= 2 && fields[0] == "blob" && long.TryParse(fields[1], out long bytes) && bytes > 100L * 1048576)
                throw new IOException("過去の履歴に100MiBを超えるファイルがあります 履歴は自動変更しません: " + (fields.Length > 2 ? fields[2] : ""));
        }
        string branch = (await runner.RunAsync(root, ["symbolic-ref", "--short", "HEAD"], token)).Output.Trim();
        progress?.Report(new("アップロード中"));
        await runner.RunAsync(root, ["push", "--set-upstream", "origin", branch], token, fullName: fullName, accountID: identity.ID);
    }

    public async Task RestoreCredentialsAsync(string root, GitProjectBinding binding, CancellationToken token) {
        var current = await runner.RunAsync(root, ["config", "--local", "--get-all", "credential.https://github.com.helper"], token, allowFailure: true);
        string[] values = current.ExitCode == 0 ? current.Output.TrimEnd('\r', '\n').Split('\n') : [];
        if (!values.SequenceEqual(new[] { "", binding.CredentialHelper })) return;
        await runner.RunAsync(root, ["config", "--local", "--unset-all", "credential.https://github.com.helper"], token, allowFailure: true);
        foreach (string helper in binding.PreviousHelpers)
            await runner.RunAsync(root, ["config", "--local", "--add", "credential.https://github.com.helper", helper], token);
    }
}
