using System.Net;
using System.Text.Json.Nodes;
using NEMEngine.Distribution;

internal static class GitHubAccountChecks {
    public static async Task RunAsync(string root, Action<bool, string> check) {
        string directory = Path.Combine(root, "account-migration");
        string file = Path.Combine(directory, "github-session.json");
        var app = new GitHubAppConfiguration { ClientID = "multiple-account-contract", AppURL = "https://github.com/apps/nemlauncher" };
        var credentials = new GitHubContractChecks.MemoryCredentials();
        credentials.Write(app.ClientID + "/1", new("account-one-token", "account-one-refresh", DateTimeOffset.MaxValue));
        DistributionStorage.Write(file, new GitHubIdentity(1, "first", "First"));
        int next = 2, refreshes = 0;
        using var client = new HttpClient(new GitHubContractChecks.Handler((request, body) => {
            string path = request.RequestUri!.AbsolutePath;
            if (path == "/login/device/code") return GitHubContractChecks.JSON("{\"user_code\":\"ACCOUNT\",\"device_code\":\"temporary\",\"verification_uri\":\"https://github.com/login/device\",\"interval\":1,\"expires_in\":900}");
            if (path == "/login/oauth/access_token") {
                check(!body.Contains("client_secret"), "複数アカウントの更新へ秘密鍵を送信しました");
                if (body.Contains("refresh_token")) {
                    ++refreshes;
                    check(body.Contains("account-one-refresh"), "別のアカウントの更新トークンを使用しました");
                    return GitHubContractChecks.JSON("{\"access_token\":\"refreshed-one\",\"refresh_token\":\"new-one\",\"expires_in\":3600}");
                }
                return GitHubContractChecks.JSON($"{{\"access_token\":\"account-{next}\",\"refresh_token\":\"refresh-{next}\",\"expires_in\":3600}}");
            }
            if (path == "/user") return GitHubContractChecks.JSON($"{{\"id\":{next},\"login\":\"second\",\"name\":\"Second\",\"avatar_url\":\"https://avatars.githubusercontent.com/u/2\"}}");
            check(request.Headers.Authorization?.Parameter == "account-one-token", "選択中の別アカウントでAPIを呼びました");
            if (path == "/user/installations") return GitHubContractChecks.JSON("{\"installations\":[{\"account\":{\"id\":1,\"login\":\"first\",\"type\":\"User\"}}]}");
            return new(HttpStatusCode.NotFound);
        }));
        var session = new GitHubSession(directory, client, app, credentials, new GitHubContractChecks.FastTime());
        check(session.Identity?.ID == 1 && session.Accounts.Count == 1, "旧形式の接続を読み込めません");
        var bound = session.ForAccount(1);
        session.SelectAccount(1);
        check(JsonNode.Parse(File.ReadAllText(file))?["accounts"] is JsonArray, "旧接続情報を移行できません");
        await session.ConnectAsync(_ => { }, default);
        check(session.Accounts.Count == 2 && session.Identity?.ID == 2 && credentials.Values.Count == 2, "アカウント追加で以前の接続を失いました");
        check(bound.Identity?.ID == 1 && await bound.GetAccessTokenAsync(default) == "account-one-token", "切替でプロジェクトの認証が変わりました");
        var owners = await new GitHubRepositories(client, session).ForAccount(1).GetOwnersAsync(default);
        check(owners.Single().Login == "first" && session.Identity?.ID == 2, "APIのアカウントを固定できません");
        await session.ConnectAsync(_ => { }, default);
        check(session.Accounts.Count == 2 && session.Accounts.Single(item => item.ID == 2).AvatarURL.Length != 0, "再認証で重複登録したかアイコン情報を失いました");
        await session.GetAccessTokenAsync(1, default, true);
        check(refreshes == 1 && credentials.Read(app.ClientID + "/2")!.AccessToken == "account-2", "更新時に別アカウントの資格情報を変更しました");
        check(!File.ReadAllText(file).Contains("token") && !File.ReadAllText(file).Contains("refresh"), "公開設定へ認証情報を書き込みました");
        session.Disconnect(2);
        check(session.Identity?.ID == 1 && session.Accounts.Count == 1 && credentials.Read(app.ClientID + "/2") == null,
            "選択したアカウントだけを解除できません");
        await GitHubContractChecks.RejectAsync(() => session.GetAccessTokenAsync(2, default), check, "解除したアカウントを別のアカウントへ自動切替しました");
        await GitHubContractChecks.RejectAsync(() => { session.SelectAccount(999); return Task.CompletedTask; }, check, "未登録アカウントを選択できました");
        using var stop = new CancellationTokenSource();
        stop.Cancel();
        next = 3;
        await GitHubContractChecks.RejectAsync(() => session.ConnectAsync(_ => { }, stop.Token), check, "キャンセル後に登録しました");
        check(session.Accounts.Count == 1, "キャンセルでアカウント一覧が変わりました");
    }
}
