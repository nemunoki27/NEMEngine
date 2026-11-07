using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Data;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using NEMEngine.Distribution;
using NEMEngine.Launcher;

// Popupと一覧全体を実際の描画結果で確認する
internal static class LauncherVisualChecks {
    public static void Run(string repository, string root, Action<bool, string> check) {
        string directory = Path.Combine(root, "VisualState");
        var store = new LauncherStore(Path.Combine(directory, "state"));
        DistributionStorage.Write(Path.Combine(store.Root, "github-session.json"), new GitHubAccounts { ActiveAccountID = 7, Accounts = [
            new(7, "team-development", "First") { AvatarURL = "https://invalid.example/avatar.png" },
            new(8, "personal-account", "Second") { AvatarURL = "https://invalid.example/avatar.png" }] });
        string descriptor = Path.Combine(directory, "長い保存先の表示確認用フォルダー", "チーム制作プロジェクト.nemproject");
        Directory.CreateDirectory(Path.GetDirectoryName(descriptor)!);
        File.WriteAllText(descriptor, "{}");
        var project = new ProjectEntry { Name = "チーム制作プロジェクト", EngineVersion = "0.1.3", Descriptor = descriptor };
        store.Register(project);
        store.Register(project with { Name = "長い日本語のプロジェクト名も列からはみ出さずに表示する", Descriptor = descriptor + ".missing" });
        var installed = new InstalledEngine { Version = "0.1.3", Owned = true, Root = Path.Combine(directory, "engine") };
        Directory.CreateDirectory(installed.Root);
        store.Register(installed);
        store.Register(installed with { Version = "0.1.1", Owned = false });
        var catalog = new ReleaseCatalog { Engines = new[] { "0.1.3", "0.1.2", "0.1.1" }.Select(version => new EngineRelease {
            Version = version, Notes = "プロジェクトの操作と一覧表示を改善。長い説明は二行まで折り返し、隣の列へ重ならないように表示します。",
            Engine = new PackageInfo { Bytes = 42 * 1048576 }, Toolchain = new PackageInfo { Bytes = 280 * 1048576 }
        }).ToList() };
        var window = new MainWindow(store, directory, catalog) { ShowInTaskbar = false, ShowActivated = false,
            WindowStartupLocation = WindowStartupLocation.Manual, Left = 40, Top = 40 };
        try {
            window.Show();
            Pump(window);
            var tabs = (TabControl)window.FindName("Tabs");
            var projects = (DataGrid)window.FindName("Projects");
            var engines = (DataGrid)window.FindName("Engines");
            var projectRow = projects.ItemsSource.Cast<ProjectListEntry>().Single(item => item.Descriptor == descriptor);
            projectRow.SetGitLink("https://github.com/example/project");
            projects.SelectedItem = projectRow;
            Pump(window);
            Capture(window, Path.Combine(repository, "Generated/Launcher.0.1.4.Projects.png"));
            CheckList(projects, 4, check);
            var row = (DataGridRow)projects.ItemContainerGenerator.ContainerFromItem(projectRow);
            check(Descendants<Button>(row).Any(button => (string)button.Content == "開く" && button.Visibility == Visibility.Visible), "Gitのリンクボタンがありません");
            projectRow.SetGitLink("");
            Pump(window);
            check(Descendants<TextBlock>(row).Any(label => label.Text == "未作成" && label.Visibility == Visibility.Visible), "Git未作成の表示へ戻りません");
            projectRow.SetGitLink("https://github.com/example/project");
            var labels = Descendants<TextBlock>(row).ToArray();
            check(labels.Any(label => label.Text == project.Name && label.FontSize == 16) &&
                labels.Any(label => label.Text == descriptor && label.FontSize == 12), "名前と保存先を二段で表示していません");
            check(labels.Any(label => label.Text == "利用可能"), "利用できるプロジェクトの状態が空欄です");
            check(row.Background is SolidColorBrush background && background.Color.R == 0x3A,
                "選択色が行全体へ反映されません");

            // 旧テンプレートの描画も残して白い帯と比較する
            var reference = window.CreateProjectMenu(project);
            reference.Style = new Style(typeof(ContextMenu)) { Setters = {
                new Setter(Control.BackgroundProperty, new SolidColorBrush(Color.FromRgb(37, 37, 37))),
                new Setter(Control.ForegroundProperty, Brushes.White), new Setter(Control.PaddingProperty, new Thickness(4)) } };
            OpenMenu(reference, row);
            int oldWhite = CountLeftWhite(Capture(reference, Path.Combine(repository, "Generated/Launcher.0.1.4.Menu.Before.png")));
            reference.IsOpen = false;
            Pump(window);
            CheckMenu(window.CreateProjectMenu(project), row, repository, "Project", check);

            tabs.SelectedIndex = 1;
            Pump(window);
            CheckList(engines, 4, check);
            var engineRow = (DataGridRow)engines.ItemContainerGenerator.ContainerFromIndex(0);
            engines.SelectedIndex = 0;
            Pump(window);
            Capture(window, Path.Combine(repository, "Generated/Launcher.0.1.4.Engines.png"));
            CheckMenu(window.CreateEngineMenu("0.1.3"), engineRow, repository, "EngineInstalled", check);
            CheckMenu(window.CreateEngineMenu("0.1.2"), engineRow, repository, "EngineAvailable", check);
            var disabled = window.CreateEngineMenu("0.1.1");
            CheckMenu(disabled, engineRow, repository, "EngineDisabled", check);
            check(!((MenuItem)disabled.Items[0]).IsEnabled && ((MenuItem)disabled.Items[0]).Foreground is SolidColorBrush muted &&
                muted.Color.R >= 120 && muted.Color.R < 180, "無効な項目の文字を背景と区別できません");

            // 最小幅でも横スクロールを必要としない
            window.Width = window.MinWidth;
            Pump(window);
            CheckList(engines, 4, check);
            Capture(window, Path.Combine(repository, "Generated/Launcher.0.1.4.Engines.Minimum.png"));
            tabs.SelectedIndex = 0;
            Pump(window);
            CheckList(projects, 4, check);
            Capture(window, Path.Combine(repository, "Generated/Launcher.0.1.4.Projects.Minimum.png"));
            check(projects.Columns[0].SortMemberPath == "Name" && engines.Columns[1].SortMemberPath == "Status",
                "テンプレート列のソート対象がありません");
            var view = CollectionViewSource.GetDefaultView(projects.ItemsSource);
            view.SortDescriptions.Add(new SortDescription("Name", ListSortDirection.Descending));
            Pump(window);
            check(view.Cast<ProjectListEntry>().First().Name.StartsWith("長い日本語"), "名前の列で並べ替えられません");
            tabs.SelectedIndex = 3;
            window.Width = 1100;
            Pump(window);
            var accounts = (ComboBox)window.FindName("GitHubAccounts");
            check(accounts.Items.Count == 2 && (long)accounts.SelectedValue == 7, "設定に複数アカウントが表示されません");
            accounts.SelectedIndex = 1;
            Pump(window);
            check((long)accounts.SelectedValue == 8, "設定で使用アカウントを切り替えられません");
            Capture(window, Path.Combine(repository, "Generated/Launcher." + DistributionVersion.Current + ".Settings.png"));
            Console.WriteLine($"Popup確認: 旧メニュー左端の白い画素 {oldWhite} / 修正後 0");
        } finally { window.Close(); }
    }

    private static void CheckList(DataGrid grid, int columns, Action<bool, string> check) {
        grid.UpdateLayout();
        var row = (DataGridRow)grid.ItemContainerGenerator.ContainerFromIndex(0);
        check(row != null && grid.RowHeight == 64 && row.ActualHeight >= 64 && row.ActualHeight <= 65 && grid.Columns.Count == columns,
            $"一覧の行の高さか列数が正しくありません: 高さ={row?.ActualHeight}, 列数={grid.Columns.Count}");
        check(Descendants<ScrollViewer>(grid).All(scroll => scroll.ComputedHorizontalScrollBarVisibility != Visibility.Visible),
            "一覧に不要な横スクロールが出ています");
        check(Descendants<DataGridCell>(row!).All(cell => cell.Background is SolidColorBrush brush && brush.Color.A == 0),
            "セルの選択色が行の色へ重なっています");
    }

    private static void CheckMenu(ContextMenu menu, FrameworkElement target, string repository, string name, Action<bool, string> check) {
        try {
            OpenMenu(menu, target);
            check(menu.IsOpen && PresentationSource.FromVisual(menu) != null, "実際のPopupが開いていません");
            var bitmap = Capture(menu, Path.Combine(repository, $"Generated/Launcher.0.1.4.Menu.{name}.png"));
            check(CountLeftWhite(bitmap) == 0, "右クリックメニューの左端に白い帯が残っています");
            check(menu.Items.Cast<MenuItem>().All(item => item.ActualWidth > 0 && item.ActualHeight >= 30),
                "右クリックメニューの項目が欠けています");
            var first = (MenuItem)menu.Items[0];
            first.Focus();
            Pump(menu);
            Capture(menu, Path.Combine(repository, $"Generated/Launcher.0.1.4.Menu.{name}.Focused.png"));
        } finally { menu.IsOpen = false; Pump(target); }
    }

    private static void OpenMenu(ContextMenu menu, FrameworkElement target) {
        menu.PlacementTarget = target;
        menu.Placement = PlacementMode.Right;
        menu.IsOpen = true;
        Pump(menu);
    }

    private static void Pump(FrameworkElement element) {
        element.UpdateLayout();
        Dispatcher.CurrentDispatcher.Invoke(() => { }, DispatcherPriority.ApplicationIdle);
        element.UpdateLayout();
    }

    private static RenderTargetBitmap Capture(FrameworkElement element, string path) {
        var bitmap = new RenderTargetBitmap((int)Math.Ceiling(element.ActualWidth), (int)Math.Ceiling(element.ActualHeight),
            96, 96, PixelFormats.Pbgra32);
        bitmap.Render(element);
        var encoder = new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create(bitmap));
        using var stream = File.Create(path);
        encoder.Save(stream);
        return bitmap;
    }

    private static int CountLeftWhite(BitmapSource bitmap) {
        int stride = bitmap.PixelWidth * 4;
        var pixels = new byte[stride * bitmap.PixelHeight];
        bitmap.CopyPixels(pixels, stride, 0);
        int count = 0;
        for (int y = 4; y < bitmap.PixelHeight - 4; ++y) {
            for (int x = 4; x < Math.Min(14, bitmap.PixelWidth - 4); ++x) {
                int offset = y * stride + x * 4;
                if (pixels[offset] > 210 && pixels[offset + 1] > 210 && pixels[offset + 2] > 210 && pixels[offset + 3] > 240) ++count;
            }
        }
        return count;
    }

    private static IEnumerable<T> Descendants<T>(DependencyObject parent) where T : DependencyObject {
        for (int index = 0; index < VisualTreeHelper.GetChildrenCount(parent); ++index) {
            var child = VisualTreeHelper.GetChild(parent, index);
            if (child is T match) yield return match;
            foreach (var nested in Descendants<T>(child)) yield return nested;
        }
    }
}
