using System.Net;
using NEMEngine.Distribution;

internal static class GitNativeChecks {
    internal static string? LauncherOverride { get; set; }
    internal static string Launcher(string repository) => Path.GetFullPath(LauncherOverride ?? Path.Combine(repository, "Project/Tools/NEM.Launcher/bin/Release/net10.0-windows/NEMLauncher.exe"));

    public static async Task RunAsync(string repository, string root, Action<bool, string> check) {
        var manifest = DistributionDefaults.GitTools;
        string packageRoot = Path.Combine(repository, "Generated/Distribution/GitTools");
        using var client = new HttpClient(new PackageFiles(packageRoot));
        string toolState = Path.Combine(root, "GitToolsState");
        var toolService = new GitToolchainService(toolState, client);
        var tools = await toolService.EnsureAsync(null, default);
        check(toolService.FindInstalled() == tools, "GitとLFSの専用配置を再利用できません");
        var runner = new GitCommandRunner(tools, Launcher(repository), toolState);
        var version = await runner.RunAsync(root, ["--version"], default);
        check(version.Output.Contains(manifest.GitVersion.TrimStart('v')), "指定したGitを使用していません");
        string projectRoot = Path.Combine(root, "日本語 Git project");
        Directory.CreateDirectory(projectRoot);
        var project = Descriptor(projectRoot, "Demo");
        await runner.RunAsync(projectRoot, ["init", "--initial-branch=main", "--", projectRoot], default);
        File.WriteAllText(Path.Combine(projectRoot, ".gitignore"), "# user rule\ncustom/\n");
        Directory.CreateDirectory(Path.Combine(projectRoot, "Library"));
        File.WriteAllText(Path.Combine(projectRoot, "Library/ignored.bin"), "generated");
        File.WriteAllBytes(Path.Combine(projectRoot, "Texture.PNG"), Enumerable.Range(0, 65536).Select(index => (byte)(index * 13)).ToArray());
        File.WriteAllText(Path.Combine(projectRoot, "Keep.cs.meta"), "stable guid");
        var worktree = new GitWorktree(runner);
        var preview = await worktree.PreviewAsync(projectRoot, default);
        check(preview.Files.Any(file => file.Path == "Texture.PNG" && file.LFS) && preview.Files.All(file => !file.Path.StartsWith("Library/")),
            "LFS対象か生成物の除外が正しくありません");
        check(File.ReadAllText(Path.Combine(projectRoot, ".gitignore")).Contains("# user rule"), "既存の除外設定を失いました");
        File.Copy(Path.Combine(projectRoot, "Texture.PNG"), Path.Combine(projectRoot, "Deleted.png"));
        File.Copy(Path.Combine(projectRoot, "Texture.PNG"), Path.Combine(projectRoot, "Modified.png"));
        await runner.RunAsync(projectRoot, ["config", "--local", "user.name", "NEMLauncher test"], default);
        await runner.RunAsync(projectRoot, ["config", "--local", "user.email", "test@example.invalid"], default);
        await runner.RunAsync(projectRoot, ["add", "--all"], default);
        await runner.RunAsync(projectRoot, ["commit", "-m", "LFS fixture"], default);
        var stored = await runner.RunAsync(projectRoot, ["show", "HEAD:Texture.PNG"], default);
        check(stored.Output.StartsWith("version https://git-lfs.github.com/spec/v1"), "大文字の拡張子をLFSポインターへ変換できません");
        string remote = Path.Combine(root, "remote.git");
        await runner.RunAsync(root, ["init", "--bare", "--initial-branch=main", remote], default);
        string remoteURL = new Uri(remote + Path.DirectorySeparatorChar).AbsoluteUri.TrimEnd('/');
        await runner.RunAsync(projectRoot, ["remote", "add", "origin", remoteURL], default);
        await runner.RunAsync(projectRoot, ["push", "--set-upstream", "origin", "main"], default);
        check(Directory.Exists(Path.Combine(remote, "lfs/objects")), "pre-push経由でLFSを送信できません");
        string cloned = Path.Combine(root, "cloned");
        await runner.RunAsync(root, ["clone", "--", remoteURL, cloned], default);
        await new GitWorktree(runner).ConfigureLFSAsync(cloned, default);
        await runner.RunAsync(cloned, ["lfs", "pull"], default);
        await runner.RunAsync(cloned, ["lfs", "fsck"], default);
        check(File.ReadAllBytes(Path.Combine(cloned, "Texture.PNG")).SequenceEqual(File.ReadAllBytes(Path.Combine(projectRoot, "Texture.PNG"))),
            "LFSファイルを実データとして取得できません");
        check(File.ReadAllText(Path.Combine(cloned, "Keep.cs.meta")) == "stable guid", "cloneでGUIDを変更しました");

        // 遠隔作成後の失敗は履歴を巻き戻さず再開可能な記録へ残す
        string failedRoot = Path.Combine(root, "PendingPublish");
        Directory.CreateDirectory(failedRoot);
        var failedProject = Descriptor(failedRoot, "Pending");
        await runner.RunAsync(failedRoot, ["init", "--initial-branch=main", "--", failedRoot], default);
        await runner.RunAsync(failedRoot, ["config", "--local", "user.name", "NEMLauncher test"], default);
        await runner.RunAsync(failedRoot, ["config", "--local", "user.email", "test@example.invalid"], default);
        using (var large = File.Create(Path.Combine(failedRoot, "old.data"))) large.SetLength(101L * 1048576);
        await runner.RunAsync(failedRoot, ["add", "--all"], default);
        await runner.RunAsync(failedRoot, ["commit", "-m", "existing history"], default);
        var store = new LauncherStore(Path.Combine(root, "GitState"));
        store.Register(failedProject);
        string sessionRoot = Path.Combine(root, "GitSession");
        DistributionStorage.Write(Path.Combine(sessionRoot, "github-session.json"), new GitHubIdentity(7, "fixture", "Fixture"));
        var session = new GitHubSession(sessionRoot, client, DistributionDefaults.GitHub, new GitHubContractChecks.MemoryCredentials());
        var gateway = new RepositoryFixture();
        var service = new GitProjectService(store, toolService, session, gateway, Launcher(repository));
        await GitHubContractChecks.RejectAsync(() => service.CreateAsync(failedProject, new(7, "fixture", false, true), "pending", "", true, null, default),
            check, "過去の大容量blobを上書きして送信しました");
        var pending = GitProjectBinding.Read(failedRoot)!;
        check(gateway.Created == 1 && pending.Repository != null && !pending.UploadComplete, "作成済みの遠隔リポジトリを記録していません");
        File.AppendAllText(Path.Combine(failedRoot, ".gitignore"), "# custom ignore\n");
        string ignore = File.ReadAllText(Path.Combine(failedRoot, ".gitignore"));
        Directory.CreateDirectory(Path.Combine(failedRoot, ".github"));
        File.WriteAllText(Path.Combine(failedRoot, ".github/workflow.yml"), "old workflow");
        File.WriteAllText(Path.Combine(failedRoot, ".gitmodules"), "# old configuration");
        File.WriteAllText(Path.Combine(failedRoot, ".lfsconfig"), "# old LFS configuration\n");
        await GitHubContractChecks.RejectAsync(() => service.DeleteRemoteAsync(failedProject, "wrong/name", null, default), check, "確認文字列が異なるのに削除しました");
        check(gateway.Deleted == 0, "名前の確認前に削除しました");
        gateway.DenyDelete = true;
        await GitHubContractChecks.RejectAsync(() => service.DeleteRemoteAsync(failedProject, "fixture/pending", null, default), check, "遠隔削除の失敗を無視しました");
        check(Directory.Exists(Path.Combine(failedRoot, ".git")) && File.ReadAllText(Path.Combine(failedRoot, ".gitignore")) == ignore,
            "GitHub側の削除前にローカル設定を初期化しました");
        gateway.DenyDelete = false;
        using (File.Open(Path.Combine(failedRoot, ".gitignore"), FileMode.Open, FileAccess.Read, FileShare.Read))
            await GitHubContractChecks.RejectAsync(() => service.DeleteRemoteAsync(failedProject, "fixture/pending", null, default), check, "ローカル初期化の失敗を検出しません");
        check(gateway.Deleted == 1 && service.ReadBinding(failedProject)?.RemoteDeleted == true, ".git外に削除完了の記録がありません");
        using (File.Open(Path.Combine(failedRoot, ".github/workflow.yml"), FileMode.Open, FileAccess.Read, FileShare.Read))
            await GitHubContractChecks.RejectAsync(() => service.DeleteRemoteAsync(failedProject, "fixture/pending", null, default), check, ".git削除後の失敗を検出しません");
        check(!Directory.Exists(Path.Combine(failedRoot, ".git")) && service.ReadBinding(failedProject)?.RemoteDeleted == true,
            ".gitが消えた後に初期化の記録を失いました");
        await service.DeleteRemoteAsync(failedProject, "fixture/pending", null, default);
        check(gateway.Deleted == 1 && service.ReadBinding(failedProject) == null && !Directory.Exists(Path.Combine(failedRoot, ".git")), "初期化の再開で履歴を消せません");
        check(File.Exists(failedProject.Descriptor) && new FileInfo(Path.Combine(failedRoot, "old.data")).Length == 101L * 1048576,
            "Git削除で制作ファイルが消えました");
        foreach (string name in new[] { ".gitignore", ".gitattributes" })
            check(File.ReadAllText(Path.Combine(failedRoot, name)) == File.ReadAllText(Path.Combine(repository, "Templates/ManagedProject/Common", name)), "新規作成時のGit設定へ戻せません: " + name);
        check(!Directory.Exists(Path.Combine(failedRoot, ".github")) && !File.Exists(Path.Combine(failedRoot, ".gitmodules")) &&
            !File.Exists(Path.Combine(failedRoot, ".lfsconfig")), "追加されたGit設定が残っています");

        // 既存プロジェクトの使用アカウントを切り替える
        store.Register(project);
        DistributionStorage.Write(Path.Combine(sessionRoot, "github-session.json"), new GitHubAccounts { ActiveAccountID = 7,
            Accounts = [new(7, "fixture", "Fixture"), new(8, "second", "Second")] });
        await runner.RunAsync(projectRoot, ["remote", "set-url", "origin", "https://github.com/fixture/pending.git"], default);
        new GitProjectBinding { Repository = new(11, "fixture", "pending", true, true, true), AccountID = 7, UploadComplete = true }.Save(projectRoot);
        string head = (await runner.RunAsync(projectRoot, ["rev-parse", "HEAD"], default)).Output;
        gateway.AllowPush = false;
        await GitHubContractChecks.RejectAsync(() => service.ChangeAccountAsync(project, 8, null, default), check, "権限のないアカウントへ変更しました");
        check(GitProjectBinding.Read(projectRoot)!.AccountID == 7, "変更失敗で元の使用アカウントを失いました");
        gateway.AllowPush = true;
        await service.ChangeAccountAsync(project, 8, null, default);
        check(GitProjectBinding.Read(projectRoot)!.AccountID == 8 && session.Identity?.ID == 7 &&
            (await runner.RunAsync(projectRoot, ["rev-parse", "HEAD"], default)).Output == head, "アカウント変更で履歴か全体の選択を変更しました");
        await service.ChangeAccountAsync(project, 7, null, default);
        File.Delete(Path.Combine(projectRoot, "Deleted.png"));
        File.WriteAllText(Path.Combine(projectRoot, "Modified.png"), "modified asset");
        File.WriteAllText(Path.Combine(projectRoot, "Texture.PNG"), stored.Output);
        await service.DeleteRemoteAsync(project, "fixture/pending", null, default);
        check(File.ReadAllBytes(Path.Combine(projectRoot, "Texture.PNG")).SequenceEqual(File.ReadAllBytes(Path.Combine(cloned, "Texture.PNG"))), "Git削除前にLFSポインターを実データへ戻せません");
        check(!File.Exists(Path.Combine(projectRoot, "Deleted.png")) && File.ReadAllText(Path.Combine(projectRoot, "Modified.png")) == "modified asset",
            "Git削除で未commitの削除か編集を取り消しました");
        await GitCredentialChecks.RunAsync(repository, root, check);
        Console.WriteLine("Git実機確認: LFS・アカウント変更・履歴消去・初期設定復元・失敗からの再開を検証");
    }

    internal static ProjectEntry Descriptor(string root, string name) {
        string path = Path.Combine(root, name + ".nemproject");
        DistributionStorage.Write(path, new { schemaVersion = 1, projectGuid = Guid.NewGuid().ToString("N"), name, engineVersion = "0.1.3",
            assetsDirectory = "GameAssets", packagesDirectory = "Packages", projectSettingsDirectory = "ProjectSettings" });
        return ProjectService.Read(path);
    }

    private sealed class RepositoryFixture : IGitHubRepositories {
        public int Created, Deleted;
        public bool DenyDelete;
        public bool AllowPush = true;
        private readonly GitHubRepository repository_ = new(11, "fixture", "pending", true, true, true);
        public Task<GitHubRepository> GetAsync(string fullName, CancellationToken token) => Task.FromResult(repository_ with { Push = AllowPush });
        public Task<GitHubRepository> CreateAsync(GitHubOwner owner, string name, string description, bool isPrivate, CancellationToken token) {
            ++Created; return Task.FromResult(repository_);
        }
        public Task DeleteAsync(GitHubRepository repository, CancellationToken token) {
            if (DenyDelete) throw new IOException("fixture: denied");
            ++Deleted; return Task.CompletedTask;
        }
    }

    private sealed class PackageFiles(string directory) : HttpMessageHandler {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token) {
            string path = Path.Combine(directory, Path.GetFileName(request.RequestUri!.AbsolutePath));
            var response = new HttpResponseMessage(HttpStatusCode.OK) { Content = new StreamContent(File.OpenRead(path)) };
            response.Content.Headers.ContentLength = new FileInfo(path).Length;
            return Task.FromResult(response);
        }
    }
}
