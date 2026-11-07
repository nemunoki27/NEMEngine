using System.IO;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;
using NEMEngine.Distribution;

namespace NEMEngine.Launcher;

public partial class MainWindow {
    private readonly Dictionary<string, string> savedAssetPaths_ = new(StringComparer.Ordinal);

    private sealed record AssetRow(AssetRelease Release) {
        public string Name => Release.Name;
        public string Version => Release.Version;
        public string DownloadSize => $"{Release.Package.Bytes / 1048576.0:F1} MB";
    }

    private void RefreshAssets() {
        var selected = (Assets.SelectedItem as AssetRow)?.Release;
        var rows = catalog_.Assets.GroupBy(asset => asset.ID)
            .Select(group => new AssetRow(group.MaxBy(asset => Version.Parse(asset.Version))!))
            .OrderBy(row => row.Name, StringComparer.CurrentCulture).ToArray();
        Assets.ItemsSource = rows;
        Assets.SelectedItem = rows.FirstOrDefault(row => row.Release.ID == selected?.ID) ?? rows.FirstOrDefault();
        UpdateAssetDetails();
    }

    private void SelectAsset(object sender, SelectionChangedEventArgs e) => UpdateAssetDetails();

    private void UpdateAssetDetails() {
        var asset = (Assets.SelectedItem as AssetRow)?.Release;
        AssetName.Text = asset?.Name ?? "アセットがありません";
        AssetDescription.Text = asset?.Description ?? "";
        AssetVersion.Text = asset == null ? "" : "版: " + asset.Version;
        AssetEngine.Text = asset == null ? "" : "推奨エンジン: " +
            (asset.RecommendedEngineVersion.Length == 0 ? "指定なし" : asset.RecommendedEngineVersion);
        AssetFileName.Text = asset?.FileName ?? "";
        AssetDownload.IsEnabled = asset != null;
        AssetOpenFolder.IsEnabled = asset != null && savedAssetPaths_.TryGetValue(asset.Package.Sha256, out string? path) && File.Exists(path);
    }

    private async void DownloadAsset(object sender, RoutedEventArgs e) {
        if ((Assets.SelectedItem as AssetRow)?.Release is not { } asset || operation_ != null) return;
        var dialog = new SaveFileDialog { Title = "アセットZIPの保存先", Filter = "ZIPファイル|*.zip", FileName = asset.FileName,
            DefaultExt = ".zip", AddExtension = true, OverwritePrompt = true };
        if (dialog.ShowDialog(this) != true) return;
        string path = dialog.FileName;
        await RunAsync(async (progress, token) => {
            await new AssetDownloadService(client_).SaveAsync(asset, Path.Combine(store_.Root, "Downloads"), path, true, progress, token);
            savedAssetPaths_[asset.Package.Sha256] = path;
        });
    }

    private void OpenAssetFolder(object sender, RoutedEventArgs e) {
        if ((Assets.SelectedItem as AssetRow)?.Release is { } asset && savedAssetPaths_.TryGetValue(asset.Package.Sha256, out string? path))
            OpenPath(Path.GetDirectoryName(path)!);
    }
}
