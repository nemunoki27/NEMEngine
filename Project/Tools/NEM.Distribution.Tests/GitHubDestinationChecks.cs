using System.Net;
using System.Text.Json.Nodes;
using NEMEngine.Distribution;

internal static class GitHubDestinationChecks {
    public static async Task RunAsync(string root, Action<bool, string> check) {
        string directory = Path.Combine(root, "destination-accounts");
        DistributionStorage.Write(Path.Combine(directory, "github-session.json"), new GitHubAccounts { ActiveAccountID = 1,
            Accounts = [new(1, "first", "First"), new(2, "second", "Second")] });
        var configuration = DistributionDefaults.GitHub;
        var credentials = new GitHubContractChecks.MemoryCredentials();
        foreach (int id in new[] { 1, 2 }) credentials.Write(configuration.ClientID + "/" + id, new("owner-token-" + id, "", DateTimeOffset.MaxValue));
        bool secondInstalled = true, firstFails = false;
        string postedWith = "";
        using var client = new HttpClient(new GitHubContractChecks.Handler((request, body) => {
            string access = request.Headers.Authorization?.Parameter ?? "";
            check(access is "owner-token-1" or "owner-token-2", "作成先一覧の認証アカウントが不正です");
            bool first = access == "owner-token-1";
            if (first && firstFails) return GitHubContractChecks.JSON("{}", HttpStatusCode.Forbidden);
            if (request.Method == HttpMethod.Post) {
                postedWith = access;
                check(request.RequestUri!.AbsolutePath == "/user/repos" && JsonNode.Parse(body)?["name"]?.GetValue<string>() == "from-second",
                    "選択した個人アカウントの作成APIを使用していません");
                return GitHubContractChecks.JSON("{\"id\":20,\"owner\":{\"login\":\"second\"},\"name\":\"from-second\",\"private\":true,\"permissions\":{\"admin\":true,\"push\":true}}");
            }
            int id = first ? 1 : secondInstalled ? 2 : 1;
            string name = id == 1 ? "first" : "second";
            return GitHubContractChecks.JSON($"{{\"installations\":[{{\"account\":{{\"id\":{id},\"login\":\"{name}\",\"type\":\"User\"}}}},{{\"account\":{{\"id\":9,\"login\":\"team\",\"type\":\"Organization\"}}}}]}}");
        }));
        var session = new GitHubSession(directory, client, configuration, credentials);
        var repositories = new GitHubRepositories(client, session);
        var values = await repositories.GetDestinationsAsync(default);
        check(values.Count == 4 && values.Any(item => item.AccountID == 1 && item.Owner.Login == "first") &&
            values.Any(item => item.AccountID == 2 && item.Owner.Login == "second"), "登録した別アカウントの作成先が表示されません");
        var organizations = values.Where(item => item.Owner.Organization).ToArray();
        check(organizations.Length == 2 && organizations.Select(item => item.DisplayName).Distinct().Count() == 2, "同じOrganizationの認証先を区別できません");
        var selected = values.Single(item => item.AccountID == 2 && !item.Owner.Organization);
        var created = await repositories.ForAccount(selected.AccountID).CreateAsync(selected.Owner, "from-second", "", true, default);
        check(postedWith == "owner-token-2" && created.Owner == "second" && session.Identity?.ID == 1,
            "設定の既定アカウントで別アカウントのリポジトリを作成しました");
        secondInstalled = false;
        values = await repositories.GetDestinationsAsync(default);
        check(values.Single(item => item.AccountID == 2 && !item.Owner.Organization) is { Owner.Installed: false, Error.Length: 0 },
            "App未設定のアカウントを一覧から消しました");
        firstFails = true;
        values = await repositories.GetDestinationsAsync(default);
        check(values.Any(item => item.AccountID == 1 && item.Error.Length != 0) && values.Any(item => item.AccountID == 2 && item.Error.Length == 0),
            "1つのアカウントの認証失敗で他の作成先が消えました");
        using var stop = new CancellationTokenSource();
        stop.Cancel();
        await GitHubContractChecks.RejectAsync(() => repositories.GetDestinationsAsync(stop.Token), check, "一覧取得のキャンセルを無視しました");
    }

    public static async Task RunLiveAsync(Action<bool, string> check) {
        using var client = new HttpClient(new HttpClientHandler { AllowAutoRedirect = false }) { Timeout = TimeSpan.FromSeconds(30) };
        var session = new GitHubSession(new LauncherStore().Root, client, DistributionDefaults.GitHub);
        var values = await new GitHubRepositories(client, session).GetDestinationsAsync(default);
        foreach (var identity in session.Accounts) {
            using var request = new HttpRequestMessage(HttpMethod.Get, "https://api.github.com/user");
            GitHubSession.AddHeaders(request, await session.GetAccessTokenAsync(identity.ID, default));
            using var response = await client.SendAsync(request);
            response.EnsureSuccessStatusCode();
            var profile = JsonNode.Parse(await response.Content.ReadAsStringAsync())!;
            check(profile["id"]!.GetValue<long>() == identity.ID, "保存したアカウントと実際の認証先が一致しません");
            var destination = values.Single(item => item.AccountID == identity.ID && !item.Owner.Organization);
            check(destination.Owner.ID == identity.ID, "実アカウントの個人作成先が一致しません");
            check(destination.Error.Length == 0, "作成先の実取得に失敗しました: " + identity.Login + " / " + destination.Error);
            Console.WriteLine("作成先の実取得: " + destination.DisplayName);
        }
    }
}
