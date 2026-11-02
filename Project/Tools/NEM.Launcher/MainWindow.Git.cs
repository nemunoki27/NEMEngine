using System.IO;
using System.Net.Http;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using NEMEngine.Distribution;

namespace NEMEngine.Launcher;

public partial class MainWindow {
    private readonly HttpClient githubClient_ = new(new HttpClientHandler { AllowAutoRedirect = false }) { Timeout = TimeSpan.FromMinutes(2) };
    private GitHubSession github_ = null!;
    private GitHubRepositories repositories_ = null!;
    private GitProjectService git_ = null!;
    private bool refreshingAccounts_;
    private IReadOnlyList<GitHubAccountChoice> accountChoices_ = [];

    private void InitializeGitServices() {
        github_ = new GitHubSession(store_.Root, githubClient_, DistributionDefaults.GitHub);
        github_.MigrateAccounts();
        repositories_ = new GitHubRepositories(githubClient_, github_);
        git_ = new GitProjectService(store_, new GitToolchainService(store_.Root, client_), github_, repositories_, Environment.ProcessPath!);
    }

    private void RefreshGitHubAccount() {
        refreshingAccounts_ = true;
        try {
            accountChoices_ = github_.Accounts.Select(identity => new GitHubAccountChoice(identity)).ToArray();
            GitHubAccounts.ItemsSource = accountChoices_;
            GitHubAccounts.SelectedValue = github_.Identity?.ID;
            GitHubConnection.IsEnabled = github_.Identity != null;
            foreach (var choice in accountChoices_) _ = choice.LoadIconAsync(store_.Root, githubClient_);
        } finally { refreshingAccounts_ = false; }
    }

    private void AddGitHubAccount(object sender, RoutedEventArgs e) {
        try {
            new GitHubConnectDialog(this, github_).ShowDialog();
            RefreshGitHubAccount();
        } catch (Exception error) { ShowError(error); }
    }

    private async void DisconnectGitHubAccount(object sender, RoutedEventArgs e) {
        if (github_.Identity is { } identity) await RunAsync((_, token) => github_.DisconnectAsync(identity.ID, token));
    }

    private async void SelectGitHubAccount(object sender, SelectionChangedEventArgs e) {
        if (refreshingAccounts_ || GitHubAccounts.SelectedItem is not GitHubAccountChoice choice) return;
        await RunAsync((_, token) => github_.SelectAccountAsync(choice.Identity.ID, token));
    }

    public Task ConnectGitHubFromCommandAsync() {
        new GitHubConnectDialog(this, github_).ShowDialog();
        RefreshGitHubAccount();
        return Task.CompletedTask;
    }

    private bool EnsureGitHubConnection() {
        if (github_.Identity != null) return true;
        bool connected = new GitHubConnectDialog(this, github_).ShowDialog() == true;
        RefreshGitHubAccount();
        return connected;
    }

    private void AddProject(object sender, RoutedEventArgs e) {
        var menu = new ContextMenu { PlacementTarget = (Button)sender, Placement = PlacementMode.Bottom };
        AddMenuAction(menu, "PC内から追加", AddLocalProjectAsync);
        AddMenuAction(menu, "GitHubから取得", CloneProjectAsync);
        menu.IsOpen = true;
    }

    private async Task RenameProjectAsync(ProjectEntry project) {
        var dialog = new ProjectRenameDialog(this, project);
        if (dialog.ShowDialog() != true) return;
        string name = dialog.ProjectName;
        await RunAsync((_, token) => Task.Run(() => new ProjectService(store_).Rename(project, name, token), token));
    }

    private async Task OpenGitRepositoryAsync(ProjectEntry project) {
        if (git_.ReadBinding(project)?.RemoteDeleted != true && !EnsureGitHubConnection()) return;
        GitProjectBinding? binding = null;
        if (!await RunGitActionAsync(async (progress, token) => binding = await git_.RecognizeAsync(project, progress, token))) return;
        if (binding?.Repository != null) {
            var manage = new GitRepositoryManageDialog(this, binding, binding.Repository, accountChoices_, repositories_);
            if (manage.ShowDialog() != true) return;
            if (manage.Action == GitRepositoryAction.ChangeAccount) {
                long accountID = manage.AccountID;
                if (await RunGitActionAsync((progress, token) => git_.ChangeAccountAsync(project, accountID, progress, token)))
                    await OpenGitRepositoryAsync(project);
            } else if (manage.Action == GitRepositoryAction.Resume) {
                await RunGitActionAsync((progress, token) => git_.ResumeUploadAsync(project, progress, token));
            } else {
                string confirmation = manage.Confirmation;
                await RunGitActionAsync((progress, token) => git_.DeleteRemoteAsync(project, confirmation, progress, token));
            }
            return;
        }
        GitUploadPreview? preview = null;
        IReadOnlyList<GitHubRepositoryDestination>? destinations = null;
        long selectedAccount = github_.Identity!.ID;
        if (!await RunGitActionAsync(async (progress, token) => {
            destinations = await repositories_.GetDestinationsAsync(token);
            preview = await git_.PreviewAsync(project, progress, token);
        })) return;
        var dialog = new GitRepositoryDialog(this, project, preview!, destinations!, selectedAccount, repositories_, github_.Configuration);
        if (dialog.ShowDialog() != true) return;
        var git = git_.ForAccount(dialog.AccountID);
        var owner = dialog.RepositoryOwner;
        string name = dialog.RepositoryName, description = dialog.RepositoryDescription;
        bool isPrivate = dialog.IsPrivate;
        await RunGitActionAsync((progress, token) => git.CreateAsync(project, owner, name, description, isPrivate, progress, token));
    }

    private async Task CloneProjectAsync() {
        if (!EnsureGitHubConnection()) return;
        var git = git_.ForAccount(github_.Identity!.ID);
        var dialog = new GitCloneDialog(this, store_.State.ProjectsRoot);
        if (dialog.ShowDialog() != true) return;
        string url = dialog.RepositoryURL, parent = dialog.ParentDirectory, name = dialog.FolderName;
        ProjectEntry? project = null;
        if (!await RunGitActionAsync(async (progress, token) => project = await git.CloneAsync(url, parent, name, progress, token))) return;
        if (store_.State.Engines.Any(engine => engine.Version == project!.EngineVersion)) {
            Status.Text = "取得して登録しました 一覧から起動できます";
            return;
        }
        var release = catalog_.Engines.FirstOrDefault(engine => engine.Version == project!.EngineVersion);
        if (release != null && MessageBox.Show(this, $"使用するエンジン {project!.EngineVersion} が未導入です。インストールしますか？\n取得したプロジェクトは保存済みです。",
            "プロジェクトの取得", MessageBoxButton.YesNo) == MessageBoxResult.Yes)
            await InstallAsync(project.EngineVersion, false);
        else { Tabs.SelectedIndex = 1; Status.Text = "取得済みです 使用するエンジンを導入してから起動してください"; }
    }

    private async Task<bool> RunGitActionAsync(Func<IProgress<TransferProgress>, CancellationToken, Task> action) {
        bool completed = false;
        await RunAsync(async (progress, token) => { await action(progress, token); completed = true; });
        return completed;
    }
}
