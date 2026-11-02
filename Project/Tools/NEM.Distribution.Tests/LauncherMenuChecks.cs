using System.Windows;
using System.Windows.Controls;
using NEMEngine.Distribution;
using NEMEngine.Launcher;

internal static class LauncherMenuChecks {
    public static void Run(string root, Action<bool, string> check) {
        var store = new LauncherStore(Path.Combine(root, "MenuState"));
        var installed = new InstalledEngine { Version = "0.1.3", Owned = true, Root = Path.Combine(root, "MenuEngine") };
        Directory.CreateDirectory(installed.Root);
        store.Register(installed);
        store.Register(installed with { Version = "0.1.1", Owned = false });
        store.Register(installed with { Version = "0.1.0", Root = Path.Combine(root, "MissingEngine") });
        var first = new ProjectEntry { Name = "First", Descriptor = Path.Combine(root, "First.nemproject"), EngineVersion = "0.1.3" };
        var second = first with { Name = "Second", Descriptor = Path.Combine(root, "Second.nemproject") };
        store.Register(first);
        store.Register(second);
        var catalog = new ReleaseCatalog { Engines = new[] { "0.1.0", "0.1.1", "0.1.2", "0.1.3" }
            .Select(version => new EngineRelease { Version = version }).ToList() };
        var window = new MainWindow(store, root, catalog);
        var tabs = (TabControl)window.FindName("Tabs");
        var projectPanel = (DockPanel)((TabItem)tabs.Items[0]).Content;
        var enginePanel = (DockPanel)((TabItem)tabs.Items[1]).Content;
        check(Buttons(projectPanel).SequenceEqual(new[] { "新規作成", "既存プロジェクトを追加" }), "プロジェクト上部のボタンが揃っていません");
        check(Buttons(enginePanel).SequenceEqual(new[] { "エンジンバージョンの更新" }), "エンジン上部に旧ボタンが残っています");
        var grid = (DataGrid)window.FindName("Projects");
        grid.SelectedItem = first;
        var row = new DataGridRow { Item = second };
        window.PrepareRowMenu(row);
        check(row.IsSelected && Headers(row.ContextMenu).SequenceEqual(new[] { "名前変更", "起動", "プロジェクトの削除", "Gitリポジトリの作成", "エンジンバージョンの変更", "フォルダーで開く" }),
            "右クリックしたプロジェクト行の操作が揃っていません");
        check(grid.ContextMenu == null && ((DataGrid)window.FindName("Engines")).ContextMenu == null, "一覧の空白にも操作メニューがあります");
        check(Headers(window.CreateEngineMenu("0.1.2")).SequenceEqual(new[] { "インストール" }), "未インストール版の操作が正しくありません");
        check(Headers(window.CreateEngineMenu("0.1.3")).SequenceEqual(new[] { "修復", "削除" }), "インストール済み版の操作が正しくありません");
        check(((MenuItem)window.CreateEngineMenu("0.1.0").Items[0]).IsEnabled, "実体のない登録済み版を修復できません");
        check(!((MenuItem)window.CreateEngineMenu("0.1.1").Items[0]).IsEnabled, "外部登録した版を直接修復できます");
        using (File.Open(Path.Combine(installed.Root, "engine.json"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.Read)) {
            check(window.CreateEngineMenu("0.1.3").Items.Cast<MenuItem>().All(item => !item.IsEnabled), "使用中の版を修復・削除できます");
        }
        var menu = window.CreateEngineMenu("0.1.3");
        var available = (MenuItem)menu.Items[0];
        available.Measure(new Size(200, 50));
        available.ApplyTemplate();
        var border = (Border)available.Template.FindName("MenuBorder", available);
        check(menu.Background is System.Windows.Media.SolidColorBrush brush && brush.Color.R < 96 && border != null,
            "右クリックメニューに暗色の配色が適用されません");
        var settingsGrid = (Grid)((TabItem)tabs.Items[2]).Content;
        var settings = (StackPanel)settingsGrid.Children.OfType<ScrollViewer>().Single().Content;
        var report = settingsGrid.Children.OfType<Button>().Single();
        check((string)report.Content == "バグ報告" && report.HorizontalAlignment == HorizontalAlignment.Right && Grid.GetRow(report) == 1,
            "設定の右下にバグ報告ボタンがありません");
        check(settings.Children.OfType<WrapPanel>().Single().Children.OfType<Button>().Select(button => (string)button.Content)
            .SequenceEqual(new[] { "ランチャーを更新", "ログフォルダーを開く", "マニュアルを開く" }), "設定タブを変更しました");
        window.Close();
    }

    private static IEnumerable<string> Buttons(DockPanel panel) => panel.Children.OfType<WrapPanel>().Single().Children
        .OfType<Button>().Select(button => (string)button.Content);
    private static IEnumerable<string> Headers(ContextMenu menu) => menu.Items.Cast<MenuItem>().Select(item => (string)item.Header);
}
