using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using NEMEngine.Distribution;
using NEMEngine.Launcher;

internal static class LauncherContentChecks {
    public static void Run(string repository, string root, Action<bool, string> check) {
        var package = new PackageInfo { ID = "fixture", Url = "https://unit.invalid/asset.zip", Sha256 = new string('a', 64),
            Bytes = 24 * 1048576, UnpackedBytes = 48 * 1048576 };
        var first = new SampleRelease { ID = "sample", Name = "昼夜のサンプル", Description = "太陽と月の動きを確認できます。",
            Version = "1.0.0", EngineVersion = "1.2.3", Package = package };
        var latest = first with { Version = "1.1.0" };
        var other = first with { ID = "other", EngineVersion = "1.2.4", Name = "別のエンジン版" };
        var asset = new AssetRelease { ID = "texture", Name = "石材のテクスチャ", Description = "ベースカラー・法線・質感のテクスチャをまとめたZIPです。",
            Version = "1.0.0", RecommendedEngineVersion = "1.2.3", FileName = "stone-textures.zip", Package = package };
        var catalog = new ReleaseCatalog { Samples = [first, latest, other], Assets = [asset] };
        var window = new MainWindow(new LauncherStore(Path.Combine(root, "ContentUI")), root, catalog) {
            ShowActivated = false, ShowInTaskbar = false
        };
        window.Show();
        var dialog = new ProjectDialog(window, ["1.2.3", "1.2.4", "1.2.5"], root, samples: catalog.Samples) { ShowActivated = false, ShowInTaskbar = false };
        try {
            ((TabControl)window.FindName("Tabs")).SelectedIndex = 2;
            window.UpdateLayout();
            System.Windows.Threading.Dispatcher.CurrentDispatcher.Invoke(() => { }, System.Windows.Threading.DispatcherPriority.ApplicationIdle);
            window.UpdateLayout();
            var assets = (DataGrid)window.FindName("Assets");
            check(assets.Items.Count == 1 && ((TextBlock)window.FindName("AssetName")).Text == asset.Name,
                "アセットの一覧または詳細がありません");
            check(assets.Columns[0].ActualWidth >= 180, "アセット名の列が狭くなっています");
            check(((TextBlock)window.FindName("AssetEngine")).Text.Contains("1.2.3") &&
                ((TextBlock)window.FindName("AssetFileName")).Text == asset.FileName, "アセットの推奨版またはZIP名がありません");
            check(((Button)window.FindName("AssetDownload")).IsEnabled && !((Button)window.FindName("AssetOpenFolder")).IsEnabled,
                "未保存アセットのボタン状態が正しくありません");
            Capture(window, repository, "Assets");
            dialog.Show();
            dialog.VersionSelector.SelectedItem = "1.2.3";
            var choices = dialog.TemplateSelector.Items.OfType<ProjectDialog.TemplateChoice>().ToArray();
            check(choices.Select(choice => choice.ID).SequenceEqual(new[] { "2D", "3D", "sample" }) && choices.Last().Sample == latest,
                "エンジンに対応する最新サンプルだけを選択できません");
            check(dialog.TemplateSelector.Items.OfType<ComboBoxItem>().Any(item => item.Content is Separator && !item.IsEnabled),
                "標準テンプレートとサンプルの区切りを選択できます");
            dialog.TemplateSelector.SelectedItem = choices.Last();
            check(dialog.Sample == latest && dialog.ProjectTemplate == "sample", "サンプルを選択しても作成対象へ渡りません");
            dialog.UpdateLayout();
            Capture(dialog, repository, "SampleProject");
            dialog.VersionSelector.SelectedItem = "1.2.4";
            check(dialog.Sample == null && dialog.ProjectTemplate == "3D" &&
                dialog.TemplateSelector.Items.OfType<ProjectDialog.TemplateChoice>().Last().Sample == other,
                "版の変更後も非対応サンプルを保持しています");
            dialog.VersionSelector.SelectedItem = "1.2.5";
            check(dialog.TemplateSelector.Items.OfType<ProjectDialog.TemplateChoice>().All(choice => choice.Sample == null),
                "未配布の版にサンプルが残っています");
        } finally { dialog.Close(); window.Close(); }
    }

    private static void Capture(Window window, string repository, string name) {
        window.UpdateLayout();
        var bitmap = new RenderTargetBitmap((int)Math.Ceiling(window.ActualWidth), (int)Math.Ceiling(window.ActualHeight), 96, 96, PixelFormats.Pbgra32);
        bitmap.Render(window);
        var encoder = new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create(bitmap));
        using var stream = File.Create(Path.Combine(repository, "Generated/Launcher." + DistributionVersion.Current + "." + name + ".png"));
        encoder.Save(stream);
    }
}
