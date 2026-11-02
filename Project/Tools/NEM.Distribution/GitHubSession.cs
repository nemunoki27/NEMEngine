using System.Net.Http.Headers;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace NEMEngine.Distribution;

// Device Flowとトークン更新を所有する
public sealed class GitHubSession {
    private readonly string root_;
    private readonly HttpClient client_;
    private readonly GitHubAppConfiguration app_;
    private readonly IGitHubCredentialStore credentials_;
    private readonly TimeProvider time_;
    private readonly long accountID_;
    public string Root => root_;
    internal bool IsAccountBound => accountID_ != 0;
    private string SessionPath => Path.Combine(root_, "github-session.json");
    public IReadOnlyList<GitHubIdentity> Accounts => ReadAccounts().Accounts;
    public GitHubIdentity? Identity {
        get {
            var state = ReadAccounts();
            long accountID = accountID_ == 0 ? state.ActiveAccountID : accountID_;
            return state.Accounts.FirstOrDefault(item => item.ID == accountID);
        }
    }
    public GitHubIdentity? FindAccount(long accountID) => Accounts.FirstOrDefault(item => item.ID == accountID);
    public GitHubAppConfiguration Configuration => app_;

    public GitHubSession(string root, HttpClient client, GitHubAppConfiguration app, IGitHubCredentialStore? credentials = null, TimeProvider? time = null, long accountID = 0) {
        root_ = root;
        client_ = client;
        app_ = app;
        time_ = time ?? TimeProvider.System;
        accountID_ = accountID;
        credentials_ = credentials ?? (OperatingSystem.IsWindows() ? new WindowsCredentialStore() : throw new PlatformNotSupportedException());
    }

    public GitHubSession ForAccount(long accountID) {
        if (accountID <= 0) throw new ArgumentException("GitHubアカウントを選択してください");
        return new(root_, client_, app_, credentials_, time_, accountID);
    }

    public void MigrateAccounts() {
        using var operation = OperationFileLock.AcquireAsync(root_, "github-accounts", CancellationToken.None).GetAwaiter().GetResult();
        try {
            if (File.Exists(SessionPath) && JsonNode.Parse(File.ReadAllText(SessionPath)) is JsonObject data && !data.ContainsKey("accounts"))
                DistributionStorage.Write(SessionPath, ReadAccounts());
        } catch (JsonException) { }
    }

    private GitHubAccounts ReadAccounts() {
        try {
            if (!File.Exists(SessionPath)) return new();
            var data = JsonNode.Parse(File.ReadAllText(SessionPath))!.AsObject();
            if (data.ContainsKey("accounts")) {
                var state = data.Deserialize<GitHubAccounts>(DistributionStorage.JsonOptions)!;
                return state with { Accounts = (state.Accounts ?? []).Where(item => item is { ID: > 0 } && !string.IsNullOrEmpty(item.Login)).DistinctBy(item => item.ID).ToList() };
            }
            var previous = data.Deserialize<GitHubIdentity>(DistributionStorage.JsonOptions);
            return previous is { ID: > 0 } && !string.IsNullOrEmpty(previous.Login) ? new() { ActiveAccountID = previous.ID, Accounts = [previous] } : new();
        } catch (Exception error) when (error is IOException or InvalidDataException or UnauthorizedAccessException or System.Text.Json.JsonException or InvalidOperationException) { return new(); }
    }

    public void SelectAccount(long accountID) => SelectAccountAsync(accountID, CancellationToken.None).GetAwaiter().GetResult();

    public async Task SelectAccountAsync(long accountID, CancellationToken token) {
        await using var operation = await OperationFileLock.AcquireAsync(root_, "github-accounts", token);
        var state = ReadAccounts();
        if (!state.Accounts.Any(item => item.ID == accountID)) throw new InvalidOperationException("GitHubアカウントを追加してください");
        DistributionStorage.Write(SessionPath, state with { ActiveAccountID = accountID });
    }

    public async Task<GitHubIdentity> ConnectAsync(Action<GitHubDeviceCode> showCode, CancellationToken token) {
        if (app_.ClientID.Length == 0) throw new InvalidOperationException("GitHub Appの接続設定がありません ランチャーを更新してください");
        var device = await OAuthAsync("device/code", new() { ["client_id"] = app_.ClientID, ["scope"] = "offline_access" }, token);
        string userCode = Required(device, "user_code");
        string deviceCode = Required(device, "device_code");
        string verification = Required(device, "verification_uri");
        if (verification != "https://github.com/login/device") throw new InvalidDataException("GitHubの認証先が一致しません");
        showCode(new(userCode, verification));
        int interval = Math.Max(1, device["interval"]?.GetValue<int>() ?? 5);
        var expires = time_.GetUtcNow().AddSeconds(device["expires_in"]?.GetValue<int>() ?? 900);
        while (time_.GetUtcNow() < expires) {
            await Task.Delay(TimeSpan.FromSeconds(interval), time_, token);
            var response = await OAuthAsync("oauth/access_token", new() { ["client_id"] = app_.ClientID,
                ["device_code"] = deviceCode, ["grant_type"] = "urn:ietf:params:oauth:grant-type:device_code" }, token);
            string? error = response["error"]?.GetValue<string>();
            if (error == "authorization_pending") continue;
            if (error == "slow_down") { interval += 5; continue; }
            if (error == "access_denied") throw new OperationCanceledException("GitHubへの接続をキャンセルしました", token);
            if (error != null) throw new IOException("GitHubへの接続に失敗しました: " + error);
            var value = ParseToken(response);
            using var request = new HttpRequestMessage(HttpMethod.Get, "https://api.github.com/user");
            AddHeaders(request, value.AccessToken);
            using var result = await client_.SendAsync(request, token);
            result.EnsureSuccessStatusCode();
            var user = JsonNode.Parse(await result.Content.ReadAsStringAsync(token))!.AsObject();
            var identity = new GitHubIdentity(user["id"]!.GetValue<long>(), Required(user, "login"),
                user["name"]?.GetValue<string>() ?? Required(user, "login")) { AvatarURL = user["avatar_url"]?.GetValue<string>() ?? "" };
            await using var operation = await OperationFileLock.AcquireAsync(root_, "github-accounts", token);
            await using var account = await OperationFileLock.AcquireAsync(root_, "github:" + identity.ID, token);
            var accounts = ReadAccounts().Accounts;
            accounts.RemoveAll(item => item.ID == identity.ID);
            accounts.Add(identity);
            credentials_.Write(Key(identity.ID), value);
            DistributionStorage.Write(SessionPath, new GitHubAccounts { Accounts = accounts, ActiveAccountID = identity.ID });
            return identity;
        }
        throw new IOException("認証コードの期限が切れました もう一度接続してください");
    }

    public async Task<string> GetAccessTokenAsync(CancellationToken token, bool forceRefresh = false) {
        var identity = Identity ?? throw new InvalidOperationException("GitHubへ接続してください");
        await using var operation = await OperationFileLock.AcquireAsync(root_, "github:" + identity.ID, token);
        var value = credentials_.Read(Key(identity.ID)) ?? throw new InvalidOperationException("GitHubへ接続し直してください");
        if (!forceRefresh && value.ExpiresAt > time_.GetUtcNow().AddMinutes(2)) return value.AccessToken;
        if (value.RefreshToken.Length == 0) throw new InvalidOperationException("GitHubの認証期限が切れました 接続し直してください");
        var response = await OAuthAsync("oauth/access_token", new() { ["client_id"] = app_.ClientID,
            ["grant_type"] = "refresh_token", ["refresh_token"] = value.RefreshToken }, token);
        if (response["error"] != null) throw new InvalidOperationException("GitHubの認証を更新できません 接続し直してください");
        var refreshed = ParseToken(response);
        credentials_.Write(Key(identity.ID), refreshed);
        return refreshed.AccessToken;
    }

    public Task<string> GetAccessTokenAsync(long accountID, CancellationToken token, bool forceRefresh = false) =>
        ForAccount(accountID).GetAccessTokenAsync(token, forceRefresh);

    public void Disconnect() { if (Identity is { } identity) Disconnect(identity.ID); }

    public void Disconnect(long accountID) => DisconnectAsync(accountID, CancellationToken.None).GetAwaiter().GetResult();

    public async Task DisconnectAsync(long accountID, CancellationToken token) {
        await using var operation = await OperationFileLock.AcquireAsync(root_, "github-accounts", token);
        await using var account = await OperationFileLock.AcquireAsync(root_, "github:" + accountID, token);
        var state = ReadAccounts();
        credentials_.Delete(Key(accountID));
        state.Accounts.RemoveAll(item => item.ID == accountID);
        DistributionStorage.Write(SessionPath, state with { ActiveAccountID = state.ActiveAccountID == accountID ? state.Accounts.FirstOrDefault()?.ID ?? 0 : state.ActiveAccountID });
    }

    internal static void AddHeaders(HttpRequestMessage request, string accessToken) {
        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", accessToken);
        request.Headers.UserAgent.ParseAdd("NEMLauncher/" + DistributionVersion.Current);
        request.Headers.Accept.ParseAdd("application/vnd.github+json");
        request.Headers.Add("X-GitHub-Api-Version", "2026-03-10");
    }

    private string Key(long accountID) => app_.ClientID + "/" + accountID;
    private static string Required(JsonObject value, string key) => value[key]?.GetValue<string>() ?? throw new InvalidDataException("GitHubの応答に必要な項目がありません: " + key);
    private GitHubToken ParseToken(JsonObject value) => new(Required(value, "access_token"),
        value["refresh_token"]?.GetValue<string>() ?? "", value["expires_in"] == null ? DateTimeOffset.MaxValue : time_.GetUtcNow().AddSeconds(value["expires_in"]!.GetValue<int>()));

    private async Task<JsonObject> OAuthAsync(string endpoint, Dictionary<string, string> values, CancellationToken token) {
        using var request = new HttpRequestMessage(HttpMethod.Post, "https://github.com/login/" + endpoint) { Content = new FormUrlEncodedContent(values) };
        request.Headers.Accept.ParseAdd("application/json");
        request.Headers.UserAgent.ParseAdd("NEMLauncher/" + DistributionVersion.Current);
        using var response = await client_.SendAsync(request, token);
        response.EnsureSuccessStatusCode();
        return JsonNode.Parse(await response.Content.ReadAsStringAsync(token))?.AsObject() ?? throw new InvalidDataException("GitHubの認証応答を読み込めません");
    }
}
