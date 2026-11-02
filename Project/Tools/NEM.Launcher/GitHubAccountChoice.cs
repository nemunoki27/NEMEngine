using System.ComponentModel;
using System.IO;
using System.Net.Http;
using System.Text.Json.Nodes;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using NEMEngine.Distribution;

namespace NEMEngine.Launcher;

// アカウントの表示と画像取得を認証処理から分ける
internal sealed class GitHubAccountChoice(GitHubIdentity identity) : INotifyPropertyChanged {
    public GitHubIdentity Identity => identity;
    public ImageSource Icon { get; private set; } = CreateFallback();
    public event PropertyChangedEventHandler? PropertyChanged;

    public async Task LoadIconAsync(string root, HttpClient client) {
        try {
            string cache = Path.Combine(root, "Avatars", identity.ID + ".png");
            byte[] bytes;
            if (File.Exists(cache) && File.GetLastWriteTimeUtc(cache) > DateTime.UtcNow.AddDays(-1)) bytes = await File.ReadAllBytesAsync(cache);
            else {
                using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
                string url = identity.AvatarURL;
                if (url.Length == 0) {
                    using var profile = new HttpRequestMessage(HttpMethod.Get, "https://api.github.com/user/" + identity.ID);
                    profile.Headers.UserAgent.ParseAdd("NEMLauncher/" + DistributionVersion.Current);
                    profile.Headers.Add("X-GitHub-Api-Version", "2026-03-10");
                    using var response = await client.SendAsync(profile, timeout.Token);
                    response.EnsureSuccessStatusCode();
                    url = JsonNode.Parse(await response.Content.ReadAsStringAsync(timeout.Token))?["avatar_url"]?.GetValue<string>() ?? "";
                }
                if (!Uri.TryCreate(url, UriKind.Absolute, out var uri) || uri.Scheme != "https" || uri.Port != 443 || uri.UserInfo.Length != 0 ||
                    uri.Host is not ("avatars.githubusercontent.com" or "github.com")) return;
                // 画像の取得先へ認証ヘッダーを渡さない
                using var image = await client.GetAsync(uri, HttpCompletionOption.ResponseHeadersRead, timeout.Token);
                image.EnsureSuccessStatusCode();
                if (image.Content.Headers.ContentLength > 2 * 1048576) return;
                using var downloaded = new MemoryStream();
                using var stream = await image.Content.ReadAsStreamAsync(timeout.Token);
                byte[] block = new byte[16384];
                int length;
                while ((length = await stream.ReadAsync(block, timeout.Token)) != 0) {
                    if (downloaded.Length + length > 2 * 1048576) return;
                    await downloaded.WriteAsync(block.AsMemory(0, length), timeout.Token);
                }
                bytes = downloaded.ToArray();
            }
            using var source = new MemoryStream(bytes);
            var bitmap = new BitmapImage();
            bitmap.BeginInit();
            bitmap.CacheOption = BitmapCacheOption.OnLoad;
            bitmap.DecodePixelWidth = 64;
            bitmap.StreamSource = source;
            bitmap.EndInit();
            bitmap.Freeze();
            Icon = bitmap;
            PropertyChanged?.Invoke(this, new(nameof(Icon)));
            if (!File.Exists(cache) || File.GetLastWriteTimeUtc(cache) <= DateTime.UtcNow.AddDays(-1)) {
                Directory.CreateDirectory(Path.GetDirectoryName(cache)!);
                var encoder = new PngBitmapEncoder();
                encoder.Frames.Add(BitmapFrame.Create(bitmap));
                using var file = File.Create(cache);
                encoder.Save(file);
            }
        } catch (Exception error) when (error is IOException or HttpRequestException or OperationCanceledException or UnauthorizedAccessException or
            System.Text.Json.JsonException or NotSupportedException or ArgumentException or InvalidOperationException or FormatException) { }
    }

    private static ImageSource CreateFallback() {
        var drawing = new DrawingGroup();
        drawing.Children.Add(new GeometryDrawing(Brushes.Gray, null, new EllipseGeometry(new System.Windows.Point(16, 10), 6, 6)));
        drawing.Children.Add(new GeometryDrawing(Brushes.Gray, null, new EllipseGeometry(new System.Windows.Point(16, 27), 12, 10)));
        drawing.Freeze();
        return new DrawingImage(drawing);
    }
}
