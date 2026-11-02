using System.Windows;
using System.Windows.Controls;
using NEMEngine.Distribution;

namespace NEMEngine.Launcher;

internal sealed class GitRepositoryDialog : LauncherDialog {
    private readonly ComboBox owner_ = new() { DisplayMemberPath = "DisplayName" };
    private readonly TextBox name_ = new();
    private readonly TextBox description_ = new();
    private readonly ComboBox visibility_ = new() { ItemsSource = new[] { "Private", "Public" }, SelectedIndex = 0 };
    public GitHubOwner RepositoryOwner => ((GitHubRepositoryDestination)owner_.SelectedItem).Owner;
    public long AccountID => ((GitHubRepositoryDestination)owner_.SelectedItem).AccountID;
    public string RepositoryName => name_.Text.Trim();
    public string RepositoryDescription => description_.Text.Trim();
    public bool IsPrivate => visibility_.SelectedIndex == 0;

    public GitRepositoryDialog(Window owner, ProjectEntry project, GitUploadPreview preview, IReadOnlyList<GitHubRepositoryDestination> destinations,
        long preferredAccountID, GitHubRepositories repositories, GitHubAppConfiguration app) : base(owner, "Gitリポジトリの作成", 720) {
        void ApplyDestinations(IReadOnlyList<GitHubRepositoryDestination> values, long accountID, long ownerID = 0) {
            owner_.ItemsSource = values;
            owner_.SelectedItem = values.FirstOrDefault(item => item.AccountID == accountID &&
                (ownerID == 0 ? !item.Owner.Organization : item.Owner.ID == ownerID)) ?? (ownerID == 0 ? values.FirstOrDefault() : null);
            if (owner_.SelectedItem == null) Error.Text = "作成先を選び直してください";
        }
        owner_.SelectionChanged += (_, _) => Error.Text = (owner_.SelectedItem as GitHubRepositoryDestination)?.Error ?? "";
        ApplyDestinations(destinations, preferredAccountID);
        Field("作成先", owner_);
        var refresh = new Button { Content = "作成先を更新" };
        var install = new Button { Content = "GitHub Appを設定" };
        var links = new System.Windows.Controls.Primitives.UniformGrid { Columns = 2 };
        links.Children.Add(install); links.Children.Add(refresh); Panel.Children.Add(links);
        install.Click += (_, _) => OpenURL(app.InstallationURL);
        bool closed = false;
        var stop = new CancellationTokenSource();
        Closed += (_, _) => { closed = true; stop.Cancel(); stop.Dispose(); };
        refresh.Click += async (_, _) => {
            refresh.IsEnabled = false;
            var previous = owner_.SelectedItem as GitHubRepositoryDestination;
            try {
                var values = await repositories.GetDestinationsAsync(stop.Token);
                if (!closed) ApplyDestinations(values, previous?.AccountID ?? preferredAccountID, previous?.Owner.ID ?? 0);
            } catch (OperationCanceledException) { }
            catch (Exception error) { if (!closed) Error.Text = error.Message; }
            finally { if (!closed) refresh.IsEnabled = true; }
        };
        if (System.Text.RegularExpressions.Regex.IsMatch(project.Name, @"\A[A-Za-z0-9_.-]{1,100}\z")) name_.Text = project.Name;
        Field("リポジトリ名（英数字・ハイフン・アンダースコア・ピリオド）", name_);
        Field("説明（任意）", description_);
        Field("公開範囲", visibility_);
        var files = new DataGrid { Height = 190, RowHeight = 28, ColumnHeaderHeight = 30, Margin = new(4, 4, 4, 0),
            ItemsSource = preview.Files.Select(file => new { file.Path, Size = file.Deleted ? "削除" : $"{file.Bytes / 1048576.0:F2} MB", Storage = file.LFS ? "Git LFS" : "Git" }).ToArray() };
        files.Columns.Add(new DataGridTextColumn { Header = "送信対象", Binding = new System.Windows.Data.Binding("Path"), Width = new DataGridLength(1, DataGridLengthUnitType.Star) });
        files.Columns.Add(new DataGridTextColumn { Header = "サイズ", Binding = new System.Windows.Data.Binding("Size"), Width = 110 });
        files.Columns.Add(new DataGridTextColumn { Header = "管理", Binding = new System.Windows.Data.Binding("Storage"), Width = 90 });
        Field($"送信するファイル: {preview.Files.Count}件 / {preview.Bytes / 1048576.0:F1} MB", files);
        var accept = new Button { Content = "作成", IsDefault = true };
        accept.Click += (_, _) => {
            try {
                if (owner_.SelectedItem is not GitHubRepositoryDestination selected) throw new ArgumentException("作成先を選択してください");
                if (selected.Error.Length != 0) throw new ArgumentException(selected.Error);
                if (!selected.Owner.Installed) throw new ArgumentException("選択した作成先へGitHub Appをインストールし、作成先を更新してください");
                GitHubRepositories.ParseURL("https://github.com/" + selected.Owner.Login + "/" + RepositoryName);
                DialogResult = true;
            } catch (ArgumentException error) { Error.Text = error.Message; }
        };
        Actions(accept, CancelButton());
    }
}

internal enum GitRepositoryAction { Delete, Resume, ChangeAccount }

internal sealed class GitRepositoryManageDialog : LauncherDialog {
    private readonly TextBox confirmation_ = new();
    private readonly ComboBox account_ = new() { SelectedValuePath = "Identity.ID" };
    public string Confirmation => confirmation_.Text;
    public long AccountID => (account_.SelectedItem as GitHubAccountChoice)?.Identity.ID ?? 0;
    public GitRepositoryAction Action { get; private set; }

    public GitRepositoryManageDialog(Window owner, GitProjectBinding binding, GitHubRepository actual,
        IReadOnlyList<GitHubAccountChoice>? accounts = null, GitHubRepositories? repositories = null) : base(owner, "Gitリポジトリの削除", 640) {
        Description(actual.FullName + "\n" + actual.WebURL);
        bool validated = repositories == null;
        bool closed = false;
        var stop = new CancellationTokenSource();
        Closed += (_, _) => { closed = true; stop.Cancel(); stop.Dispose(); };
        if (accounts != null && !binding.RemoteDeleted) {
            account_.SetResourceReference(ItemsControl.ItemTemplateProperty, "GitHubAccountTemplate");
            account_.ItemsSource = accounts;
            account_.SelectedValue = binding.AccountID;
            var row = new DockPanel();
            var change = new Button { Content = "変更", IsEnabled = false };
            DockPanel.SetDock(change, Dock.Right);
            row.Children.Add(change);
            row.Children.Add(account_);
            account_.SelectionChanged += (_, _) => change.IsEnabled = AccountID != 0 && AccountID != binding.AccountID;
            change.Click += (_, _) => { Action = GitRepositoryAction.ChangeAccount; DialogResult = true; };
            Field("このプロジェクトのGitHubアカウント", row);
        }
        var open = new Button { Content = "GitHubで開く" };
        open.Click += (_, _) => OpenURL(actual.WebURL);
        Panel.Children.Add(open);
        Button? resume = null;
        if (!binding.UploadComplete && !binding.RemoteDeleted) {
            Description("初回アップロードが完了していません。作成済みのリポジトリへの送信を再開できます。");
            resume = new Button { Content = "初回アップロードを再開", IsEnabled = validated && actual.Push };
            resume.Click += (_, _) => { Action = GitRepositoryAction.Resume; DialogResult = true; };
            Panel.Children.Add(resume);
        }
        Field("削除するリポジトリ名を入力: " + actual.FullName, confirmation_);
        var accept = new Button { Content = binding.RemoteDeleted ? "初期化を再開" : "GitHub上のリポジトリを削除", IsEnabled = false };
        void UpdateActions() {
            bool matching = accounts == null || AccountID == binding.AccountID;
            accept.IsEnabled = (binding.RemoteDeleted || (validated && actual.Admin && matching)) && Confirmation == actual.FullName;
            if (resume != null) resume.IsEnabled = validated && actual.Push && matching;
        }
        confirmation_.TextChanged += (_, _) => UpdateActions();
        account_.SelectionChanged += (_, _) => UpdateActions();
        accept.Click += (_, _) => { Action = GitRepositoryAction.Delete; DialogResult = true; };
        Actions(accept, CancelButton());
        Loaded += async (_, _) => {
            if (repositories == null || binding.RemoteDeleted) return;
            try {
                actual = await repositories.ForAccount(binding.AccountID).GetAsync(actual.FullName, stop.Token);
                if (actual.ID != binding.Repository!.ID) throw new InvalidOperationException("リポジトリの識別情報が変わりました");
                if (closed) return;
                validated = true;
                if (!actual.Admin) Error.Text = "このアカウントには削除する管理権限がありません";
                UpdateActions();
            } catch (Exception error) { if (!closed) Error.Text = error.Message; }
        };
    }
}
