using System.Net;
using System.Text.Json.Nodes;
using NEMEngine.Distribution;

internal static class GitHubContractChecks {
    internal sealed class MemoryCredentials : IGitHubCredentialStore {
        internal readonly Dictionary<string, GitHubToken> Values = new();
        public GitHubToken? Read(string key) => Values.GetValueOrDefault(key);
        public void Write(string key, GitHubToken token) => Values[key] = token;
        public void Delete(string key) => Values.Remove(key);
    }

    internal sealed class FastTime : TimeProvider {
        private long ticks_ = new DateTime(2026, 11, 1, 0, 0, 0, DateTimeKind.Utc).Ticks;
        public override DateTimeOffset GetUtcNow() => new(new DateTime(Interlocked.Read(ref ticks_), DateTimeKind.Utc));
        public override ITimer CreateTimer(TimerCallback callback, object? state, TimeSpan dueTime, TimeSpan period) =>
            new FastTimer(this, callback, state, dueTime);
        private sealed class FastTimer : ITimer {
            private bool disposed_;
            public FastTimer(FastTime owner, TimerCallback callback, object? state, TimeSpan due) {
                if (due == Timeout.InfiniteTimeSpan) return;
                Interlocked.Add(ref owner.ticks_, due.Ticks);
                ThreadPool.QueueUserWorkItem(_ => { if (!disposed_) callback(state); });
            }
            public bool Change(TimeSpan dueTime, TimeSpan period) => throw new NotSupportedException();
            public void Dispose() => disposed_ = true;
            public ValueTask DisposeAsync() { Dispose(); return ValueTask.CompletedTask; }
        }
    }

    internal sealed class Handler(Func<HttpRequestMessage, string, HttpResponseMessage> reply) : HttpMessageHandler {
        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token) =>
            reply(request, request.Content == null ? "" : await request.Content.ReadAsStringAsync(token));
    }

    internal static HttpResponseMessage JSON(string text, HttpStatusCode status = HttpStatusCode.OK) => new(status) { Content = new StringContent(text) };

    public static async Task RunAsync(string root, Action<bool, string> check) {
        var credentials = new MemoryCredentials();
        var configuration = new GitHubAppConfiguration { ClientID = "contract-client", AppURL = "https://github.com/apps/nemlauncher" };
        int polls = 0, refreshes = 0, deletes = 0;
        bool forbidden = false, identityChanged = false;
        using var client = new HttpClient(new Handler((request, body) => {
            check(!body.Contains("client_secret", StringComparison.Ordinal), "Client Secretを認証へ送信しました");
            string path = request.RequestUri!.AbsolutePath;
            if (path == "/login/device/code") return JSON("{\"user_code\":\"ABCD-EFGH\",\"device_code\":\"temporary-device\",\"verification_uri\":\"https://github.com/login/device\",\"interval\":1,\"expires_in\":900}");
            if (path == "/login/oauth/access_token") {
                if (body.Contains("refresh_token")) {
                    ++refreshes;
                    return JSON("{\"access_token\":\"refreshed-test-token\",\"refresh_token\":\"new-refresh-test-token\",\"expires_in\":3600}");
                }
                return ++polls switch { 1 => JSON("{\"error\":\"authorization_pending\"}"), 2 => JSON("{\"error\":\"slow_down\"}"),
                    _ => JSON("{\"access_token\":\"first-test-token\",\"refresh_token\":\"first-refresh-test-token\",\"expires_in\":3600}") };
            }
            if (path == "/user") return JSON("{\"id\":7,\"login\":\"example\",\"name\":\"Example\"}");
            check(request.Headers.Authorization?.Parameter is "first-test-token" or "refreshed-test-token", "認証ヘッダーがありません");
            if (path == "/user/installations") return JSON("{\"installations\":[{\"account\":{\"id\":7,\"login\":\"example\",\"type\":\"User\"}},{\"account\":{\"id\":9,\"login\":\"team\",\"type\":\"Organization\"}}]}");
            if (request.Method == HttpMethod.Post) {
                var submitted = JsonNode.Parse(body)!;
                check(submitted["private"]!.GetValue<bool>() && !submitted["auto_init"]!.GetValue<bool>(), "既定の非公開か初回commitの所有を変更しました");
                check(path == "/orgs/team/repos" || path == "/user/repos", "所有先のAPIが正しくありません");
                return RepoJSON();
            }
            if (forbidden) return JSON("{\"message\":\"Forbidden\"}", HttpStatusCode.Forbidden);
            if (request.Method == HttpMethod.Delete) { ++deletes; return new(HttpStatusCode.NoContent); }
            return RepoJSON();
            HttpResponseMessage RepoJSON() => JSON($"{{\"id\":{(identityChanged ? 99 : 10)},\"owner\":{{\"login\":\"team\"}},\"name\":\"game\",\"private\":true,\"permissions\":{{\"admin\":true,\"push\":true}}}}");
        }));
        string sessionRoot = Path.Combine(root, "github-contract");
        var session = new GitHubSession(sessionRoot, client, configuration, credentials, new FastTime());
        string code = "";
        var identity = await session.ConnectAsync(value => code = value.UserCode, default);
        check(identity.ID == 7 && code == "ABCD-EFGH" && polls == 3, "認証の待機・減速・完了を処理できません");
        check(!File.ReadAllText(Path.Combine(sessionRoot, "github-session.json")).Contains("test-token"), "トークンを設定JSONへ保存しました");
        check(await session.GetAccessTokenAsync(default) == "first-test-token" && refreshes == 0, "有効なトークンを無駄に更新しました");
        check(await session.GetAccessTokenAsync(default, true) == "refreshed-test-token" && refreshes == 1, "認証を更新できません");
        var repositories = new GitHubRepositories(client, session);
        var owners = await repositories.GetOwnersAsync(default);
        check(owners.Count == 2 && owners[1].Organization && owners.All(owner => owner.Installed), "個人とOrganizationを取得できません");
        var repository = await repositories.CreateAsync(owners[1], "game", "test", true, default);
        identityChanged = true;
        await RejectAsync(() => repositories.DeleteAsync(repository, default), check, "別IDのリポジトリを削除しました");
        check(deletes == 0, "IDの照合前にDELETEを送信しました");
        identityChanged = false;
        forbidden = true;
        await RejectAsync(() => repositories.DeleteAsync(repository, default), check, "権限不足を無視しました");
        check(deletes == 0, "権限不足の後にDELETEを送信しました");
        forbidden = false;
        await repositories.DeleteAsync(repository, default);
        check(deletes == 1, "確認済みのリポジトリを削除できません");
        foreach (string invalid in new[] { "http://github.com/a/b", "https://example.com/a/b", "https://token@github.com/a/b", "https://github.com/a/b/extra", "https://github.com/a/b?token=x" }) {
            await RejectAsync(() => Task.FromResult(GitHubRepositories.ParseURL(invalid)), check, "不正なGitHub URLを受理しました");
        }
        check(GitHubRepositories.ParseURL("https://github.com/team/game.git") == "team/game", "clone URLを解決できません");
        session.Disconnect();
        check(session.Identity == null && credentials.Values.Count == 0, "切断時に資格情報を解除していません");
        if (OperatingSystem.IsWindows()) {
            var vault = new WindowsCredentialStore();
            string key = "validation-" + Guid.NewGuid().ToString("N");
            var value = new GitHubToken("not-a-real-access-token", "not-a-real-refresh-token", DateTimeOffset.MaxValue);
            try { vault.Write(key, value); check(vault.Read(key) == value, "Windows資格情報を読み戻せません"); }
            finally { vault.Delete(key); }
            check(vault.Read(key) == null, "検証用の資格情報が残っています");
        }
    }

    internal static async Task RejectAsync(Func<Task> action, Action<bool, string> check, string message) {
        try { await action(); }
        catch (Exception error) when (error is IOException or ArgumentException or InvalidOperationException or OperationCanceledException or UnauthorizedAccessException) { check(true, message); return; }
        check(false, message);
    }
}
