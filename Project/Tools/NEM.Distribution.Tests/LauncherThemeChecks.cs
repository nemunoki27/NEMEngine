using System.IO;
using System.Runtime.ExceptionServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using NEMEngine.Distribution;
using NEMEngine.Launcher;

// 実際のWindowとテンプレートを画面表示せずに確認する
internal static class LauncherThemeChecks {
    public static void Run(string repository, string root, Action<bool, string> check) {
        Exception? failure = null;
        var thread = new Thread(() => {
            try { CheckTheme(repository, root, check); }
            catch (Exception error) { failure = error; }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        thread.Join();
        if (failure != null) ExceptionDispatchInfo.Capture(failure).Throw();
    }

    private static void CheckTheme(string repository, string root, Action<bool, string> check) {
        var app = new App(false) { ShutdownMode = ShutdownMode.OnExplicitShutdown };
        app.InitializeComponent();
        SynchronizationContext.SetSynchronizationContext(new System.Windows.Threading.DispatcherSynchronizationContext());
        var store = new LauncherStore(Path.Combine(root, "ThemeState"));
        store.State.Projects.Add(new ProjectEntry { Name = "テストプロジェクト", EngineVersion = "0.1.1",
            Descriptor = Path.Combine(root, "Test.nemproject") });
        var window = new MainWindow(store, root);
        check(IsDark(window.Background) && IsLight(window.Foreground), "メイン画面が黒背景と白文字ではありません");
        var content = (Grid)window.Content;
        window.Content = null;
        var surface = new Border { Background = window.Background, Child = content };
        System.Windows.Documents.TextElement.SetFontSize(surface, window.FontSize);
        System.Windows.Documents.TextElement.SetFontFamily(surface, window.FontFamily);
        surface.Measure(new Size(1100, 700));
        surface.Arrange(new Rect(0, 0, 1100, 700));
        surface.UpdateLayout();
        System.Windows.Threading.Dispatcher.CurrentDispatcher.Invoke(() => { }, System.Windows.Threading.DispatcherPriority.ContextIdle);
        surface.UpdateLayout();
        var tabs = (TabControl)window.FindName("Tabs");
        var selected = (TabItem)tabs.Items[0];
        selected.ApplyTemplate();
        var tabBorder = (Border)selected.Template.FindName("TabBorder", selected);
        check(IsDark(tabBorder.Background) && IsLight(selected.Foreground), "選択中のタブが読めません");
        var button = new Button { Content = "確認", IsEnabled = false };
        button.Measure(new Size(180, 50));
        button.Arrange(new Rect(0, 0, 180, 50));
        button.ApplyTemplate();
        var buttonBorder = (Border)button.Template.FindName("ButtonBorder", button);
        check(IsDark(buttonBorder.Background), "無効なボタンの背景が白くなっています");
        var dialog = new ProjectDialog(null!, new[] { "0.1.1" }, root);
        dialog.ShowInTaskbar = false;
        dialog.ShowActivated = false;
        dialog.Show();
        dialog.UpdateLayout();
        check(IsDark(dialog.Background) && IsLight(dialog.Foreground), $"作成画面の配色が揃っていません 背景={dialog.Background} 文字={dialog.Foreground} Style={dialog.Style?.TargetType} Resource={dialog.TryFindResource("LauncherWindowStyle")?.GetType()}");
        // 同じ描画結果を画像として残す
        var bitmap = new RenderTargetBitmap(1100, 700, 96, 96, PixelFormats.Pbgra32);
        bitmap.Render(surface);
        byte[] corner = new byte[4];
        bitmap.CopyPixels(new Int32Rect(0, 0, 1, 1), corner, 4, 0);
        check(corner[0] < 64 && corner[1] < 64 && corner[2] < 64 && corner[3] == 255,
            "画面の背景が暗く描画されていません");
        string image = Path.Combine(repository, "Generated/Launcher.Theme.png");
        var encoder = new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create(bitmap));
        using (var stream = File.Create(image)) encoder.Save(stream);
        LauncherMenuChecks.Run(root, check);
        LauncherVisualChecks.Run(repository, root, check);
        window.Show();
        LauncherGitChecks.Run(repository, root, window, check);
        BugReportDialogChecks.Run(root, window, check);
        var report = new BugReportDialog(window, new LauncherStore(Path.Combine(root, "ReportUI")), null) { ShowInTaskbar = false, ShowActivated = false };
        report.Show();
        report.UpdateLayout();
        check(!report.AttachesLogs && IsDark(report.Background) && IsLight(report.Foreground), "バグ報告の添付既定値か配色が正しくありません");
        var reportImage = new RenderTargetBitmap((int)Math.Ceiling(report.ActualWidth), (int)Math.Ceiling(report.ActualHeight), 96, 96, PixelFormats.Pbgra32);
        reportImage.Render(report);
        var reportEncoder = new PngBitmapEncoder();
        reportEncoder.Frames.Add(BitmapFrame.Create(reportImage));
        using (var stream = File.Create(Path.Combine(repository, "Generated/Launcher." + DistributionVersion.Current + ".BugReport.png"))) reportEncoder.Save(stream);
        report.Close();
        dialog.Close();
        window.Close();
        app.Shutdown();
    }

    private static bool IsDark(Brush brush) => brush is SolidColorBrush solid &&
        solid.Color.R < 96 && solid.Color.G < 96 && solid.Color.B < 96;
    private static bool IsLight(Brush brush) => brush is SolidColorBrush solid &&
        solid.Color.R >= 180 && solid.Color.G >= 180 && solid.Color.B >= 180;
}
