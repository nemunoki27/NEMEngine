using System.ComponentModel;
using System.IO;
using System.Text.Json.Nodes;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using static NEMEngine.DeveloperLauncher.DeveloperSession;

namespace NEMEngine.DeveloperLauncher;

public partial class MainWindow : Window {
    private readonly DeveloperSession session;
    private readonly AuthoringStore store;
    private readonly DispatcherTimer editorTimer = new() { Interval = TimeSpan.FromSeconds(3) };
    private string? editorPipe;
    private JsonObject? catalog;
    private bool busy, checkingEditor;

    internal MainWindow(string root, string? editor) {
        session = new(root);
        store = new(root);
        editorPipe = editor;
        InitializeComponent();
        session.Progress = AppendLog;
        Loaded += async (_, _) => {
            editorTimer.Tick += async (_, _) => await RefreshEditor();
            editorTimer.Start();
            _ = RefreshEditor();
            try { session.Workspace.RemoveAbandoned(); } catch (Exception error) when (error is IOException or UnauthorizedAccessException) { AppendLog(error.Message); }
            try { RefreshSources(); } catch (Exception error) { AppendLog(error.Message); }
            await Operation(LoadCatalog, "配布一覧を取得しました");
        };
        Closing += OnClosing;
        Closed += (_, _) => {
            editorTimer.Stop();
            session.DiscardBuild();
        };
    }

    internal void ActivateFromEditor(string? editor) {
        if (!string.IsNullOrEmpty(editor))
            editorPipe = editor;
        if (WindowState == WindowState.Minimized)
            WindowState = WindowState.Normal;
        Activate();
        Focus();
        _ = RefreshEditor();
    }

    private void OnClosing(object? sender, CancelEventArgs e) {
        if (!busy)
            return;
        e.Cancel = true;
        Status.Text = "実行中の処理が終わってから閉じてください";
    }

    private async Task RefreshEditor() {
        if (busy || checkingEditor)
            return;
        checkingEditor = true;
        var pipe = editorPipe;
        try {
            if (string.IsNullOrEmpty(pipe)) { EditorLabel.Text = "Editor未接続"; return; }
            var state = await Editor(pipe, new JsonObject { ["action"] = "status" });
            if (pipe == editorPipe) { EditorLabel.Text = $"接続中　{Text(state, "project")}   /   {Text(state, "scene")}"; EditorLabel.ToolTip = Text(state, "projectRoot"); }
        } catch (Exception error) {
            if (pipe == editorPipe) { EditorLabel.Text = "Editor未接続　編集元の「バージョン管理」から接続してください"; EditorLabel.ToolTip = error.Message; }
        } finally { checkingEditor = false; }
    }

    private async Task Operation(Func<Task> action, string success) {
        if (busy)
            return;
        busy = true;
        Tabs.IsEnabled = false;
        Activity.Visibility = Visibility.Visible;
        try { await action(); Status.Text = success; } catch (OperationCanceledException) { Status.Text = "キャンセルしました"; } catch (Exception ex) { Status.Text = ex.Message; AppendLog(ex.Message); } finally { Activity.Visibility = Visibility.Collapsed; Tabs.IsEnabled = true; busy = false; }
    }

    private void AppendLog(string line) {
        Status.Text = line.Length > 200 ? line[..200] + "…" : line;
        if (Log.Text.Length > 32000)
            Log.Text = Log.Text[^20000..];
        Log.AppendText(line + Environment.NewLine);
        Log.ScrollToEnd();
    }

    private static bool Confirm(Window owner, string title, string message) =>
        MessageBox.Show(owner, message, title, MessageBoxButton.OKCancel, MessageBoxImage.Warning, MessageBoxResult.Cancel) == MessageBoxResult.OK;

    private static void Cancel() => throw new OperationCanceledException();
    private void RefreshSources(string? selectId = null) {
        void Fill(ListBox box, string kind) {
            var current = (box.SelectedItem as SourceEntry)?.Id;
            var items = store.List(kind);
            box.ItemsSource = items;
            box.SelectedItem = items.FirstOrDefault(x => x.Id == selectId) ?? items.FirstOrDefault(x => x.Id == current) ?? items.FirstOrDefault();
        }
        Fill(Templates, "Template");
        Fill(Samples, "Sample");
        Fill(Assets, "Asset");
        ShowDetails();
    }

    private async Task LoadCatalog() {
        ApplyCatalog(await session.Release(new JsonObject { ["action"] = "list" }));
    }

    private void ApplyCatalog(JsonObject currentCatalog) {
        catalog = currentCatalog;
        var current = (Engines.SelectedItem as ReleaseRow)?.Version;
        var items = Entries("engines").Select(x => new ReleaseRow(Text(x, "version"), Text(x, "notes"), false))
            .Concat(Strings("pendingDeletes").Select(v => new ReleaseRow(v, "配布一覧から取り下げ済み。本体ZIPの削除を再試行できます。", true)))
            .OrderByDescending(x => Version.Parse(x.Version)).ToList();
        Engines.ItemsSource = items;
        Engines.SelectedItem = items.FirstOrDefault(x => x.Version == current) ?? items.FirstOrDefault();
        ShowDetails();
    }

    private IEnumerable<JsonObject> Entries(string key) => catalog?[key]?.AsArray().OfType<JsonObject>() ?? [];
    private IEnumerable<string> Strings(string key) => catalog?[key]?.AsArray().Select(x => x!.GetValue<string>()) ?? [];
    private void SelectionChanged(object sender, SelectionChangedEventArgs e) { if (IsLoaded) ShowDetails(); }

    private void ShowDetails() {
        var release = Engines.SelectedItem as ReleaseRow;
        EnginesTitle.Text = release?.Version ?? "公開版がありません";
        EnginesDetail.Text = release?.Notes;
        NotesButton.IsEnabled = DeleteVersionButton.IsEnabled = release is { Pending: false };
        CleanupButton.Visibility = release is { Pending: true } ? Visibility.Visible : Visibility.Collapsed;
        void Source(ListBox box, TextBlock title, TextBlock detail, string category) {
            var entry = box.SelectedItem as SourceEntry;
            title.Text = entry?.Name ?? "未登録";
            detail.Text = "";
            if (entry == null)
                return;
            var item = entry.Data;
            var lines = new List<string>();
            if (Text(item, "description").Length != 0)
                lines.Add(Text(item, "description"));
            if (category != "") {
                lines.Add("版番号　" + Text(item, "version"));
                string engine = Text(item, category == "samples" ? "engineVersion" : "recommendedEngineVersion");
                if (engine.Length != 0)
                    lines.Add("エンジン　" + engine);
                string[] published = Entries(category).Where(x => Text(x, "id") == entry.Id)
                    .Select(x => category == "samples" ? $"{Text(x, "version")}（{Text(x, "engineVersion")}）" : Text(x, "version")).ToArray();
                lines.Add("公開状態　" + (catalog == null ? "未取得" : published.Length == 0 ? "下書き" : string.Join("、", published)));
            }
            lines.Add("保存元　" + entry.Directory);
            detail.Text = string.Join(Environment.NewLine + Environment.NewLine, lines);
        }
        Source(Templates, TemplatesTitle, TemplatesDetail, "");
        Source(Samples, SamplesTitle, SamplesDetail, "samples");
        Source(Assets, AssetsTitle, AssetsDetail, "assets");
    }

    private SourceEntry Selected(ListBox box) => box.SelectedItem as SourceEntry ??
        throw new InvalidOperationException("一覧から選択してください");

    private async void CommandClick(object sender, RoutedEventArgs e) {
        var command = (string)((Button)sender).Tag;
        await Operation(async () => {
            switch (command) {
                case "RefreshCatalog":
                    await LoadCatalog();
                    break;
                case "NewRelease":
                    await PublishRelease();
                    break;
                case "EditNotes":
                case "DeleteRelease":
                case "CleanupRelease":
                    await ChangeRelease(command);
                    break;
                case "SaveTemplate":
                    await SaveScene(false);
                    break;
                case "SaveSampleScene":
                    await SaveScene(true);
                    break;
                case "OpenSource":
                    await OpenSource(Selected(Tabs.SelectedIndex == 1 ? Templates : Samples));
                    break;
                case "EditSample":
                    EditSample(Selected(Samples));
                    break;
                case "NewAsset":
                    await EditAsset(null);
                    break;
                case "EditAsset":
                    await EditAsset(Selected(Assets));
                    break;
                case "PublishSample":
                case "WithdrawSample":
                case "DeleteSample":
                case "PublishAsset":
                case "WithdrawAsset":
                    await ChangeContent(command);
                    break;
                default:
                    throw new InvalidOperationException("操作が不正です");
            }
        }, command switch {
            "NewRelease" or "PublishSample" or "PublishAsset" => "公開しました",
            "RefreshCatalog" => "配布一覧を取得しました",
            "OpenSource" => "編集用Editorを開きました",
            "WithdrawSample" or "WithdrawAsset" => "公開を停止しました",
            "DeleteRelease" or "DeleteSample" or "CleanupRelease" => "削除しました",
            _ => "保存しました"
        });
    }

    private sealed record ReleaseRow(string Version, string Notes, bool Pending) {
        public override string ToString() => Pending ? Version + "　削除待ち" : Version;
    }
}
