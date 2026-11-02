using System.Net;
using System.Net.Http.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace NEMEngine.Distribution;

// GitHub上の所有先とリポジトリだけを扱う
public sealed class GitHubRepositories(HttpClient client, GitHubSession session) : IGitHubRepositories {
    public GitHubRepositories ForAccount(long accountID) => new(client, session.ForAccount(accountID));
    IGitHubRepositories IGitHubRepositories.ForAccount(long accountID) => ForAccount(accountID);

    public async Task<IReadOnlyList<GitHubRepositoryDestination>> GetDestinationsAsync(CancellationToken token) {
        // 作成先と認証アカウントを一組で取得する
        var accounts = session.Accounts;
        var results = await Task.WhenAll(accounts.Select(async identity => {
            try {
                var owners = await ForAccount(identity.ID).GetOwnersAsync(token);
                return owners.Select(owner => new GitHubRepositoryDestination(identity.ID, identity.Login, owner)).ToArray();
            } catch (OperationCanceledException) when (!token.IsCancellationRequested) {
                return [new GitHubRepositoryDestination(identity.ID, identity.Login, new(identity.ID, identity.Login, false, false), "GitHubとの通信がタイムアウトしました")];
            } catch (Exception error) when (error is IOException or HttpRequestException or InvalidOperationException) {
                token.ThrowIfCancellationRequested();
                return [new GitHubRepositoryDestination(identity.ID, identity.Login, new(identity.ID, identity.Login, false, false), error.Message)];
            }
        }));
        return results.SelectMany(items => items).OrderBy(item => item.Owner.Organization).ThenBy(item => item.Owner.Login)
            .ThenBy(item => item.AccountLogin).ToArray();
    }

    public async Task<IReadOnlyList<GitHubOwner>> GetOwnersAsync(CancellationToken token) {
        if (!session.IsAccountBound) return await ForAccount(RequireAccount()).GetOwnersAsync(token);
        var identity = session.Identity ?? throw new InvalidOperationException("GitHubへ接続してください");
        var owners = new Dictionary<long, GitHubOwner> { [identity.ID] = new(identity.ID, identity.Login, false, false) };
        for (int page = 1; ; ++page) {
            var response = await SendAsync(HttpMethod.Get, $"user/installations?per_page=100&page={page}", null, token);
            var installations = response!["installations"]!.AsArray();
            foreach (var installation in installations) {
                var account = installation!["account"]!;
                long id = account["id"]!.GetValue<long>();
                bool organization = account["type"]!.GetValue<string>() == "Organization";
                if (organization || id == identity.ID) owners[id] = new(id, account["login"]!.GetValue<string>(), organization, true);
            }
            if (installations.Count < 100) break;
        }
        return owners.Values.OrderBy(owner => owner.Organization).ThenBy(owner => owner.Login).ToArray();
    }

    public async Task<GitHubRepository> GetAsync(string fullName, CancellationToken token) {
        if (!session.IsAccountBound) return await ForAccount(RequireAccount()).GetAsync(fullName, token);
        RequireFullName(fullName);
        return ReadRepository((await SendAsync(HttpMethod.Get, "repos/" + fullName, null, token))!);
    }

    public async Task<GitHubRepository> CreateAsync(GitHubOwner owner, string name, string description, bool isPrivate, CancellationToken token) {
        if (!session.IsAccountBound) return await ForAccount(RequireAccount()).CreateAsync(owner, name, description, isPrivate, token);
        RequireFullName(owner.Login + "/" + name);
        if (!owner.Installed) throw new InvalidOperationException("作成先へGitHub Appをインストールしてください");
        string endpoint = owner.Organization ? "orgs/" + owner.Login + "/repos" : "user/repos";
        if (!owner.Organization && owner.ID != session.Identity?.ID) throw new InvalidOperationException("接続しているアカウントの所有先を選択してください");
        return ReadRepository((await SendAsync(HttpMethod.Post, endpoint, new { name, description, @private = isPrivate, auto_init = false }, token))!);
    }

    public async Task DeleteAsync(GitHubRepository repository, CancellationToken token) {
        if (!session.IsAccountBound) { await ForAccount(RequireAccount()).DeleteAsync(repository, token); return; }
        var actual = await GetAsync(repository.FullName, token);
        if (actual.ID != repository.ID) throw new InvalidOperationException("リポジトリの識別情報が変わりました 削除を中止しました");
        if (!actual.Admin) throw new InvalidOperationException("このリポジトリを削除する管理権限がありません");
        await SendAsync(HttpMethod.Delete, "repos/" + actual.FullName, null, token);
    }

    public static string ParseURL(string value) {
        if (!Uri.TryCreate(value.Trim(), UriKind.Absolute, out var uri) || uri.Scheme != "https" || uri.Host != "github.com" ||
            uri.Port != 443 || uri.UserInfo.Length != 0 || uri.Query.Length != 0 || uri.Fragment.Length != 0)
            throw new ArgumentException("https://github.com/所有者/リポジトリ のURLを指定してください");
        string name = uri.AbsolutePath.Trim('/');
        if (name.EndsWith(".git", StringComparison.OrdinalIgnoreCase)) name = name[..^4];
        RequireFullName(name);
        return name;
    }

    private static void RequireFullName(string value) {
        if (!Regex.IsMatch(value, @"\A[A-Za-z0-9][A-Za-z0-9-]{0,99}/[A-Za-z0-9_.-]{1,100}\z") ||
            value.Split('/')[1] is "." or ".." or ".git") throw new ArgumentException("GitHubの所有者名かリポジトリ名が不正です");
    }

    private long RequireAccount() => session.Identity?.ID ?? throw new InvalidOperationException("GitHubアカウントを追加してください");

    private static GitHubRepository ReadRepository(JsonNode value) => new(value["id"]!.GetValue<long>(),
        value["owner"]!["login"]!.GetValue<string>(), value["name"]!.GetValue<string>(), value["private"]!.GetValue<bool>(),
        value["permissions"]?["admin"]?.GetValue<bool>() ?? false, value["permissions"]?["push"]?.GetValue<bool>() ?? false);

    private async Task<JsonNode?> SendAsync(HttpMethod method, string endpoint, object? body, CancellationToken token) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            string accessToken = await session.GetAccessTokenAsync(token, attempt != 0);
            using var request = new HttpRequestMessage(method, "https://api.github.com/" + endpoint);
            GitHubSession.AddHeaders(request, accessToken);
            if (body != null) request.Content = JsonContent.Create(body);
            using var response = await client.SendAsync(request, token);
            if (response.StatusCode == HttpStatusCode.Unauthorized && attempt == 0) continue;
            if (!response.IsSuccessStatusCode) {
                string message = response.StatusCode switch {
                    HttpStatusCode.Forbidden => "GitHubの操作権限がありません OrganizationのApp承認と権限を確認してください",
                    HttpStatusCode.NotFound => "リポジトリが見つからないかアクセスできません URL・招待の受諾・Appの対象を確認してください",
                    HttpStatusCode.UnprocessableEntity => "同じ名前のリポジトリがあるか、指定した項目がGitHubの制限に合いません",
                    _ => "GitHubとの通信に失敗しました 接続状態と認証を確認してください"
                };
                throw new IOException(message + $"（HTTP {(int)response.StatusCode}）");
            }
            if (response.StatusCode == HttpStatusCode.NoContent) return null;
            return JsonNode.Parse(await response.Content.ReadAsStringAsync(token));
        }
        throw new InvalidOperationException("GitHubへ接続し直してください");
    }
}
