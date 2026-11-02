namespace NEMEngine.Distribution;

public sealed record GitHubAppConfiguration {
    public string ClientID { get; init; } = "";
    public string AppURL { get; init; } = "";
    public string InstallationURL => AppURL.TrimEnd('/') + "/installations/new";
}

public sealed record GitHubIdentity(long ID, string Login, string Name) {
    public string AvatarURL { get; init; } = "";
    public string CommitEmail => $"{ID}+{Login}@users.noreply.github.com";
}

public sealed record GitHubAccounts {
    public long ActiveAccountID { get; init; }
    public List<GitHubIdentity> Accounts { get; init; } = [];
}

public sealed record GitHubToken(string AccessToken, string RefreshToken, DateTimeOffset ExpiresAt);
public sealed record GitHubDeviceCode(string UserCode, string VerificationURL);
public sealed record GitHubOwner(long ID, string Login, bool Organization, bool Installed) {
    public string DisplayName => Login + (Organization ? "（Organization）" : "（個人）") + (Installed ? "" : " App未設定");
}

public sealed record GitHubRepositoryDestination(long AccountID, string AccountLogin, GitHubOwner Owner, string Error = "") {
    public string DisplayName => Error.Length != 0 ? AccountLogin + "（要確認）" :
        Owner.DisplayName + (Owner.Organization ? " / 認証: " + AccountLogin : "");
}

public sealed record GitHubRepository(long ID, string Owner, string Name, bool Private, bool Admin, bool Push) {
    public string FullName => Owner + "/" + Name;
    public string WebURL => "https://github.com/" + FullName;
    public string CloneURL => WebURL + ".git";
}

public interface IGitHubCredentialStore {
    GitHubToken? Read(string key);
    void Write(string key, GitHubToken token);
    void Delete(string key);
}

public interface IGitHubRepositories {
    IGitHubRepositories ForAccount(long accountID) => this;
    Task<GitHubRepository> GetAsync(string fullName, CancellationToken token);
    Task<GitHubRepository> CreateAsync(GitHubOwner owner, string name, string description, bool isPrivate, CancellationToken token);
    Task DeleteAsync(GitHubRepository repository, CancellationToken token);
}
