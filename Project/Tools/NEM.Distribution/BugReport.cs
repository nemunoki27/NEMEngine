using System.Net.Http.Json;
using System.Text.Encodings.Web;
using System.Text.Json;

namespace NEMEngine.Distribution;

public sealed record BugReportLog(string Kind, string Text, bool Truncated);
public sealed record BugReportRequest {
    public string ID { get; init; } = Guid.NewGuid().ToString("D");
    public string Title { get; init; } = "";
    public string Description { get; init; } = "";
    public string Contact { get; init; } = "";
    public string Kind { get; init; } = "Launcher";
    public string ProjectName { get; init; } = "";
    public string LauncherVersion { get; init; } = DistributionVersion.Current;
    public string EngineVersion { get; init; } = "";
    public string WindowsVersion { get; init; } = Environment.OSVersion.VersionString;
    public bool AttachLogs { get; init; }
    public IReadOnlyList<BugReportLog> Logs { get; init; } = [];
}
public sealed record BugReportReceipt(string ID, string ReceivedAt);

public static class BugReports {
    public const string DefaultURL = "https://nemengine-support.nemengine-distribution.workers.dev/reports";
    public const int MaxRequestBytes = 4 * 1048576;
    public static JsonSerializerOptions Options { get; } = new(DistributionStorage.JsonOptions) { WriteIndented = false, Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping };

    public static byte[] Serialize(BugReportRequest report) {
        if (string.IsNullOrWhiteSpace(report.Title) || report.Title.Length > 200) throw new ArgumentException("タイトルを200文字以内で入力してください");
        if (string.IsNullOrWhiteSpace(report.Description) || report.Description.Length > 16000) throw new ArgumentException("現象・再現手順を16000文字以内で入力してください");
        if (report.Contact.Length > 256) throw new ArgumentException("連絡先を256文字以内で入力してください");
        if (!report.AttachLogs && report.Logs.Count != 0) throw new ArgumentException("添付OFFの報告へログを追加できません");
        byte[] data = JsonSerializer.SerializeToUtf8Bytes(report, Options);
        if (data.Length > MaxRequestBytes) throw new ArgumentException("報告の容量が4MiBを超えています");
        return data;
    }

    public static async Task<BugReportReceipt> SendAsync(HttpClient client, string url, BugReportRequest report, CancellationToken token) {
        if (!Uri.TryCreate(url, UriKind.Absolute, out var endpoint) || endpoint.Scheme != "https" || endpoint.UserInfo.Length != 0)
            throw new ArgumentException("報告先のHTTPS URLが不正です");
        using var request = new HttpRequestMessage(HttpMethod.Post, endpoint) { Content = new ByteArrayContent(Serialize(report)) };
        request.Content.Headers.ContentType = new("application/json");
        request.Headers.UserAgent.ParseAdd("NEMLauncher/" + DistributionVersion.Current);
        using var response = await client.SendAsync(request, token);
        if (!response.IsSuccessStatusCode) throw new IOException(response.StatusCode switch {
            System.Net.HttpStatusCode.TooManyRequests => "受付上限に達しています 時間をおいて再送信してください",
            System.Net.HttpStatusCode.RequestEntityTooLarge => "報告の容量が上限を超えています",
            System.Net.HttpStatusCode.BadRequest => "報告の入力内容を確認してください",
            System.Net.HttpStatusCode.Conflict => "報告内容が変わっています 入力を変更してから再送信してください",
            _ => "送信結果を確認できません 同じ内容で再試行してください"
        });
        var receipt = await response.Content.ReadFromJsonAsync<BugReportReceipt>(Options, token) ?? throw new IOException("受付結果を読み込めません");
        if (receipt.ID != report.ID || !DateTimeOffset.TryParse(receipt.ReceivedAt, out _)) throw new IOException("受付結果が一致しません");
        return receipt;
    }
}
