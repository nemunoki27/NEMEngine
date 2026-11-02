using NEMEngine.Distribution;
using NEMEngine.Launcher;

// 実際のGitHub Appで専用の非公開リポジトリを往復する
internal static class GitLiveChecks {
    public static async Task RunAsync(string repository, string root, Action<bool, string> check) {
        using var timeout = new CancellationTokenSource(TimeSpan.FromMinutes(8));
        CancellationToken token = timeout.Token;
        using var client = new HttpClient(new HttpClientHandler { AllowAutoRedirect = false }) { Timeout = TimeSpan.FromMinutes(2) };
        using var downloads = new HttpClient { Timeout = TimeSpan.FromMinutes(5) };
        string accountRoot = new LauncherStore().Root;
        var session = new GitHubSession(accountRoot, client, DistributionDefaults.GitHub);
        var identity = session.Identity ?? throw new InvalidOperationException("実機確認にはGitHubの接続が必要です");
        var avatar = new GitHubAccountChoice(identity);
        await avatar.LoadIconAsync(Path.Combine(root, "LiveAvatar"), client);
        check(avatar.Icon is System.Windows.Media.Imaging.BitmapSource, "GitHubアイコンの実画像を取得できません");
        await session.GetAccessTokenAsync(token, true);
        var gateway = new GitHubRepositories(client, session);
        var owner = (await gateway.GetOwnersAsync(token)).Single(item => item.ID == identity.ID && item.Installed);
        var tools = new GitToolchainService(accountRoot, downloads);
        var installed = await tools.EnsureAsync(null, token);
        var runner = new GitCommandRunner(installed, GitNativeChecks.Launcher(repository), accountRoot);
        var store = new LauncherStore(Path.Combine(root, "LiveState"));
        var service = new GitProjectService(store, tools, session, gateway, GitNativeChecks.Launcher(repository));
        string projectRoot = Path.Combine(root, "LiveSource");
        Directory.CreateDirectory(projectRoot);
        var project = GitNativeChecks.Descriptor(projectRoot, "SharedDemo");
        store.Register(project);
        await runner.RunAsync(projectRoot, ["init", "--initial-branch=main", "--", projectRoot], token);
        Directory.CreateDirectory(Path.Combine(projectRoot, "GameAssets"));
        byte[] image = Enumerable.Range(0, 32768).Select(index => (byte)(index * 17)).ToArray();
        File.WriteAllBytes(Path.Combine(projectRoot, "GameAssets/Shared.png"), image);
        File.WriteAllText(Path.Combine(projectRoot, "GameAssets/Shared.png.meta"), "fixed asset guid");
        string name = "nem-launcher-validation-" + Guid.NewGuid().ToString("N")[..12];
        GitHubRepository? created = null;
        try {
            Console.WriteLine("GitHub実機確認: 非公開リポジトリを作成して送信");
            await service.CreateAsync(project, owner, name, "NEMLauncher integration validation", true, null, token);
            created = GitProjectBinding.Read(projectRoot)!.Repository!;
            check(created.Private && GitProjectBinding.Read(projectRoot)!.UploadComplete, "非公開リポジトリの初回送信が完了していません");
            var actual = await gateway.GetAsync(created.FullName, token);
            check(actual.ID == created.ID && actual.Admin && actual.Push, "作成後の識別情報か操作権限が一致しません");
            Console.WriteLine("GitHub実機確認: 別フォルダーへcloneしてLFSの実データを照合");
            var cloned = await service.CloneAsync(created.WebURL, Path.Combine(root, "Team"), "参加プロジェクト", null, token);
            string clonedRoot = Path.GetDirectoryName(cloned.Descriptor)!;
            check(cloned.ProjectGUID == project.ProjectGUID && cloned.EngineVersion == project.EngineVersion,
                "参加時にProject GUIDか使用エンジンの版が変わりました");
            check(File.ReadAllBytes(Path.Combine(clonedRoot, "GameAssets/Shared.png")).SequenceEqual(image) &&
                File.ReadAllText(Path.Combine(clonedRoot, "GameAssets/Shared.png.meta")) == "fixed asset guid", "GitHubからLFSかmetaを復元できません");
            var pointer = await runner.RunAsync(projectRoot, ["show", "HEAD:GameAssets/Shared.png"], token);
            File.WriteAllText(Path.Combine(projectRoot, "GameAssets/Shared.png"), pointer.Output);
            DistributionStorage.RemoveTree(Path.Combine(projectRoot, ".git/lfs/objects"), projectRoot);
            await service.DeleteRemoteAsync(project, created.FullName, null, token);
            created = null;
            check(File.Exists(project.Descriptor) && !Directory.Exists(Path.Combine(projectRoot, ".git")) &&
                File.ReadAllBytes(Path.Combine(projectRoot, "GameAssets/Shared.png")).SequenceEqual(image), "Git削除で制作ファイルを失ったか履歴が残っています");
            await GitHubContractChecks.RejectAsync(() => gateway.GetAsync(actual.FullName, token), check, "削除した検証用リポジトリがGitHubに残っています");
            Console.WriteLine("GitHub実機確認: 作成・送信・LFS取得・遠隔削除が完了");
        } finally {
            // 途中失敗でも今回作成したIDのリポジトリだけを片付ける
            var binding = service.ReadBinding(project);
            if (binding?.RemoteDeleted == true) created = null;
            else created ??= binding?.Repository;
            if (created != null && created.Name == name && created.Owner == identity.Login) {
                using var cleanup = new CancellationTokenSource(TimeSpan.FromMinutes(1));
                await gateway.DeleteAsync(created, cleanup.Token);
            }
        }
    }
}
