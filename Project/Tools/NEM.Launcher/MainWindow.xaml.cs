using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using Microsoft.Win32;
using NEMEngine.Distribution;

namespace NEMEngine.Launcher;

public partial class MainWindow : Window {
    private readonly LauncherStore store_;
    private readonly HttpClient client_ = new() { Timeout = Timeout.InfiniteTimeSpan };
    private ReleaseCatalog catalog_ = new();
    private CancellationTokenSource? operation_;
    private LauncherConfiguration configuration_ = new();

    private sealed record EngineRow(string Version, string Status, string DownloadSize, string Notes);

    public MainWindow() : this(new LauncherStore(), Path.Combine(AppContext.BaseDirectory, "Config")) { }

    internal MainWindow(LauncherStore store, string configurationDirectory, ReleaseCatalog? catalog = null) {
        store_ = store;
        catalog_ = catalog ?? new();
        InitializeComponent();
        LauncherVersion.Text = "Launcher " + DistributionVersion.Current;
        string path = Path.Combine(configurationDirectory, "launcher.json");
        if (File.Exists(path)) configuration_ = DistributionStorage.Read<LauncherConfiguration>(path);
        if (store_.State.CatalogUrl.Length == 0 && configuration_.CatalogUrl.Length != 0) {
            store_.State.CatalogUrl = configuration_.CatalogUrl;
            store_.SaveSettings();
        }
        InitializeGitServices();
        RefreshLists();
        Activated += (_, _) => RefreshProjectGitLinks();
        Closing += (_, e) => {
            if (operation_ != null) {
                e.Cancel = true;
                MessageBox.Show(this, "実行中の操作をキャンセルしてから終了してください", "NEMLauncher");
            }
        };
        Closed += (_, _) => { gitLinks_?.Cancel(); gitLinks_?.Dispose(); client_.Dispose(); githubClient_.Dispose(); };
    }

    private async void OnLoaded(object sender, RoutedEventArgs e) {
        if (store_.State.CatalogUrl.Length != 0) await LoadCatalogAsync();
    }

    private void RefreshLists() {
        store_.RefreshProjects();
        string? selected = ProjectFromItem(Projects.SelectedItem)?.Descriptor;
        var rows = store_.State.Projects.OrderByDescending(project => project.LastOpened).Select(project => new ProjectListEntry(project)).ToArray();
        Projects.ItemsSource = rows;
        Projects.SelectedItem = rows.FirstOrDefault(row => row.Descriptor == selected);
        RefreshProjectGitLinks();
        var versions = catalog_.Engines.Select(engine => engine.Version)
            .Concat(store_.State.Engines.Select(engine => engine.Version)).Distinct().OrderByDescending(Version.Parse);
        Engines.ItemsSource = versions.Select(version => {
            var local = store_.State.Engines.FirstOrDefault(engine => engine.Version == version);
            var release = catalog_.Engines.FirstOrDefault(engine => engine.Version == version);
            long bytes = release == null ? 0 : release.Engine.Bytes + release.Toolchain.Bytes;
            return new EngineRow(version, local?.Status ?? "未インストール", bytes == 0 ? "" : $"{bytes / 1048576.0:F0} MB", release?.Notes ?? "ローカル登録");
        }).ToArray();
        InstallRoot.Text = store_.State.InstallRoot;
        ProjectsRoot.Text = store_.State.ProjectsRoot;
        RefreshGitHubAccount();
    }

    private async Task RunAsync(Func<IProgress<TransferProgress>, CancellationToken, Task> action) {
        if (operation_ != null) return;
        operation_ = new();
        Tabs.IsEnabled = false;
        CancelButton.IsEnabled = true;
        var progress = new Progress<TransferProgress>(value => { Status.Text = value.Message; Progress.Value = value.Percentage; });
        try {
            await action(progress, operation_.Token);
            Status.Text = "完了";
        } catch (OperationCanceledException) { Status.Text = "操作をキャンセルしました"; }
        catch (Exception error) { ShowError(error); }
        finally {
            operation_.Dispose();
            operation_ = null;
            Tabs.IsEnabled = true;
            CancelButton.IsEnabled = false;
            RefreshLists();
        }
    }

    public void ShowError(Exception error) {
        try {
            Directory.CreateDirectory(Path.Combine(store_.Root, "Logs"));
            File.AppendAllText(Path.Combine(store_.Root, "Logs", "launcher.log"), $"{DateTime.Now:O} {error}\n");
        } catch (Exception failure) when (failure is IOException or UnauthorizedAccessException) { }
        Status.Text = error.Message;
        MessageBox.Show(this, error.Message, "NEMLauncher", MessageBoxButton.OK, MessageBoxImage.Error);
    }

    public Task OpenDescriptorAsync(string descriptor) => RunAsync((_, _) => {
        var project = ProjectService.Read(descriptor);
        store_.Register(project);
        if (store_.State.Engines.All(engine => engine.Version != project.EngineVersion)) {
            Tabs.SelectedIndex = 1;
            MessageBox.Show(this, $"使用版 {project.EngineVersion} をインストールしてから起動してください", "NEMLauncher");
        } else new ProjectService(store_).StartEditor(project);
        return Task.CompletedTask;
    });

    private async void CreateProject(object sender, RoutedEventArgs e) {
        if (store_.State.Engines.Count == 0) { Tabs.SelectedIndex = 1; Status.Text = "先にエンジンをインストールしてください"; return; }
        var dialog = new ProjectDialog(this, store_.State.Engines.Select(engine => engine.Version), store_.State.ProjectsRoot);
        if (dialog.ShowDialog() != true) return;
        string name = dialog.ProjectName, parent = dialog.ParentDirectory, version = dialog.EngineVersion, template = dialog.ProjectTemplate;
        await RunAsync(async (_, token) => {
            var service = new ProjectService(store_);
            var project = await Task.Run(() => service.Create(name, parent, version, template), token);
            service.StartEditor(project);
        });
    }

    private async Task AddLocalProjectAsync() {
        var dialog = new OpenFileDialog { Filter = "NEMプロジェクト|*.nemproject" };
        if (dialog.ShowDialog(this) != true) return;
        await RunAsync((_, _) => { store_.Register(ProjectService.Read(dialog.FileName)); return Task.CompletedTask; });
    }

    // 選択状態ではなく右クリックした行を操作する
    private void RowMenuOpening(object sender, ContextMenuEventArgs e) => PrepareRowMenu((DataGridRow)sender);

    internal void PrepareRowMenu(DataGridRow row) {
        row.IsSelected = true;
        row.ContextMenu = ProjectFromItem(row.Item) is { } project ? CreateProjectMenu(project) : CreateEngineMenu(((EngineRow)row.Item).Version);
    }

    internal ContextMenu CreateProjectMenu(ProjectEntry project) {
        var menu = new ContextMenu();
        AddMenuAction(menu, "名前変更", () => RenameProjectAsync(project));
        AddMenuAction(menu, "起動", () => OpenDescriptorAsync(project.Descriptor));
        AddMenuAction(menu, "プロジェクトの削除", () => RemoveProjectAsync(project));
        var binding = git_.ReadBinding(project);
        AddMenuAction(menu, binding?.Repository == null ? "Gitリポジトリの作成" : "Gitリポジトリの削除", () => OpenGitRepositoryAsync(project));
        AddMenuAction(menu, "エンジンバージョンの変更", () => ChangeProjectVersionAsync(project));
        AddMenuAction(menu, "フォルダーで開く", () => {
            OpenPath(Path.GetDirectoryName(project.Descriptor)!);
            return Task.CompletedTask;
        });
        return menu;
    }

    internal ContextMenu CreateEngineMenu(string version) {
        var menu = new ContextMenu();
        var engine = store_.State.Engines.FirstOrDefault(item => item.Version == version);
        bool available = catalog_.Engines.Any(item => item.Version == version);
        if (engine == null) AddMenuAction(menu, "インストール", () => InstallAsync(version, false), available);
        else {
            bool inUse = EngineValidation.IsInUse(engine.Root);
            AddMenuAction(menu, "修復", () => InstallAsync(version, true), available && engine.Owned && !inUse);
            AddMenuAction(menu, "削除", () => RemoveEngineAsync(engine), !inUse);
        }
        return menu;
    }

    private void AddMenuAction(ContextMenu menu, string label, Func<Task> action, bool enabled = true) {
        var item = new MenuItem { Header = label, IsEnabled = enabled && operation_ == null };
        item.Click += async (_, _) => {
            if (operation_ != null) return;
            try { await action(); }
            catch (Exception error) { ShowError(error); }
        };
        menu.Items.Add(item);
    }

    private async void ProjectDoubleClick(object sender, MouseButtonEventArgs e) {
        if (e.ChangedButton != MouseButton.Left || operation_ != null) return;
        if (e.OriginalSource is DependencyObject element && IsButtonInteraction(element)) return;
        if (e.OriginalSource is DependencyObject source &&
            ItemsControl.ContainerFromElement(Projects, source) is DataGridRow row && ProjectFromItem(row.Item) is { } project) {
            await OpenDescriptorAsync(project.Descriptor);
            e.Handled = true;
        }
    }

    private async Task ChangeProjectVersionAsync(ProjectEntry project) {
        var dialog = new ProjectDialog(this, store_.State.Engines.Select(engine => engine.Version), "", project.EngineVersion);
        if (dialog.ShowDialog() != true || dialog.EngineVersion == project.EngineVersion) return;
        string version = dialog.EngineVersion;
        await RunAsync((_, token) => Task.Run(() => new ProjectService(store_).ChangeVersion(project, version), token));
    }

    private async Task RemoveProjectAsync(ProjectEntry project) {
        var service = new ProjectService(store_);
        string directory = "";
        await RunAsync((_, token) => Task.Run(() => directory = service.GetDeletionDirectory(project, token), token));
        if (directory.Length == 0) return;
        string message = Directory.Exists(directory)
            ? $"「{project.Name}」をフォルダーごとごみ箱へ移しますか？\n\n{directory}\n\n制作ファイルとGit管理情報も対象になります"
            : $"「{project.Name}」のフォルダーが見つかりません。一覧から削除しますか？\n\n{directory}";
        if (MessageBox.Show(this, message, "プロジェクトの削除", MessageBoxButton.YesNo,
            MessageBoxImage.Warning, MessageBoxResult.No) != MessageBoxResult.Yes) return;
        await RunAsync((_, token) => Task.Run(() => service.Delete(project, token), token));
    }

    private Task LoadCatalogAsync() => RunAsync(async (_, token) => catalog_ = await CatalogService.FetchAsync(client_, store_.State.CatalogUrl, token));
    private async void RefreshCatalog(object sender, RoutedEventArgs e) => await LoadCatalogAsync();

    private async Task InstallAsync(string version, bool repair) {
        var release = catalog_.Engines.FirstOrDefault(engine => engine.Version == version);
        if (release == null) { Status.Text = "この版の配布情報がありません"; return; }
        await RunAsync((progress, token) => Task.Run(() =>
            new EngineInstallService(store_, client_).InstallAsync(release, repair, progress, token), token));
    }

    private async Task RemoveEngineAsync(InstalledEngine engine) {
        string message = engine.Owned ? "エンジンを削除しますか？ プロジェクトは保持されます" : "一覧から登録を解除しますか？ フォルダーは保持されます";
        if (MessageBox.Show(this, message, "NEMLauncher", MessageBoxButton.YesNo) != MessageBoxResult.Yes) return;
        await RunAsync((_, token) => Task.Run(() => {
            if (engine.Owned) new EngineInstallService(store_, client_).Uninstall(engine);
            else store_.RemoveEngine(engine.Version);
        }, token));
    }

    private void SelectInstallRoot(object sender, RoutedEventArgs e) => SelectFolder(true);
    private void SelectProjectsRoot(object sender, RoutedEventArgs e) => SelectFolder(false);
    private void SelectFolder(bool installation) {
        var dialog = new OpenFolderDialog { InitialDirectory = installation ? store_.State.InstallRoot : store_.State.ProjectsRoot };
        if (dialog.ShowDialog(this) != true) return;
        if (installation) store_.State.InstallRoot = dialog.FolderName;
        else store_.State.ProjectsRoot = dialog.FolderName;
        store_.SaveSettings();
        RefreshLists();
    }

    private async void UpdateLauncher(object sender, RoutedEventArgs e) {
        var release = catalog_.Launcher;
        if (release == null || Version.Parse(release.Version) <= Version.Parse(DistributionVersion.Current)) { Status.Text = "新しいランチャーはありません"; return; }
        string? setup = null;
        await RunAsync(async (progress, token) => {
            setup = await new PackageTransfer(client_).DownloadAsync(release.Setup, Path.Combine(store_.Root, "Downloads"), progress, token);
        });
        if (setup == null) return;
        try {
            string executable = Path.ChangeExtension(setup, ".exe");
            File.Move(setup, executable, true);
            Process.Start(new ProcessStartInfo(executable) { UseShellExecute = true, Arguments = "/CLOSEAPPLICATIONS /RESTARTAPPLICATIONS" });
            Close();
        } catch (Exception error) { ShowError(error); }
    }

    private void OpenLogs(object sender, RoutedEventArgs e) {
        string logs = Path.Combine(store_.Root, "Logs");
        Directory.CreateDirectory(logs);
        OpenPath(logs);
    }

    private void OpenManual(object sender, RoutedEventArgs e) {
        if (Uri.TryCreate(configuration_.ManualUrl, UriKind.Absolute, out var uri) && uri.Scheme == "https") OpenPath(uri.AbsoluteUri);
        else Status.Text = "マニュアルURLはまだ設定されていません";
    }

    private void CancelOperation(object sender, RoutedEventArgs e) => operation_?.Cancel();
    private void OpenPath(string path) {
        try { Process.Start(new ProcessStartInfo(path) { UseShellExecute = true }); }
        catch (Exception error) { ShowError(error); }
    }
}

internal sealed record LauncherConfiguration {
    public string CatalogUrl { get; init; } = "";
    public string ManualUrl { get; init; } = "";
}
