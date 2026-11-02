using System.Net;
using System.Text;
using System.Text.Json;
using NEMEngine.Distribution;

internal static class BugReportChecks {
    public static async Task RunAsync(string root, Action<bool, string> check) {
        string directory = Path.Combine(root, "ReportState");
        string projectRoot = Path.Combine(root, "ReportProject");
        Directory.CreateDirectory(Path.Combine(directory, "Logs"));
        Directory.CreateDirectory(Path.Combine(projectRoot, "Saved/Logs"));
        var project = new ProjectEntry { Name = "Report", Descriptor = Path.Combine(projectRoot, "Report.nemproject"), EngineVersion = "0.1.3" };
        string secret = "ghp_abcdefghijklmnop";
        File.WriteAllText(Path.Combine(directory, "Logs/launcher.log"), "先頭 " + secret + "\n" + new string('a', 1100000) + "\n末尾");
        File.WriteAllText(Path.Combine(projectRoot, "Saved/Logs/engine.log"), projectRoot + " C:\\Users\\someone\\project access_token=example-secret Bearer example-bearer");
        File.WriteAllText(Path.Combine(projectRoot, "Saved/Logs/gameLogic.log"), "{\"path\":\"" + projectRoot.Replace("\\", "\\\\") + "\",\"password\":\"example-password\"}");
        var files = BugReportLogs.Describe(directory, project);
        check(files.Count == 3 && files[0].Bytes > BugReportLogs.MaxLogBytes, "添付候補の容量を取得していません");
        var logs = BugReportLogs.Capture(directory, project, [projectRoot], default);
        check(logs.Count == 3 && logs.All(log => Encoding.UTF8.GetByteCount(log.Text) <= BugReportLogs.MaxLogBytes), "添付の個別容量を超えています");
        check(logs[0].Truncated && logs[0].Text.Contains("先頭") && logs[0].Text.Contains("末尾") && logs[0].Text.Contains("省略"), "大きいログの先頭と末尾が残りません");
        string attached = string.Join('\n', logs.Select(log => log.Text));
        check(!attached.Contains(secret) && !attached.Contains("example-secret") && !attached.Contains("example-bearer") && !attached.Contains("example-password") &&
            !attached.Contains("someone") && !attached.Contains(projectRoot) && !attached.Contains(projectRoot.Replace("\\", "\\\\")), "秘密情報か個人パスが残っています");
        check(BugReportLogs.Capture(Path.Combine(root, "MissingLogs"), null, [], default).Count == 0, "存在しないログを添付しました");
        var report = new BugReportRequest { Title = "確認用", Description = "再現手順" };
        using (var document = JsonDocument.Parse(BugReports.Serialize(report))) {
            check(!document.RootElement.GetProperty("attachLogs").GetBoolean() && document.RootElement.GetProperty("logs").GetArrayLength() == 0,
                "添付OFFの報告にログが含まれています");
        }
        try { BugReports.Serialize(report with { Logs = logs }); throw new Exception("添付OFFへログを追加できました"); } catch (ArgumentException) { check(true, "添付OFF"); }
        check(BugReports.Serialize(report with { AttachLogs = true, Logs = logs }).Length <= BugReports.MaxRequestBytes, "報告全体の容量を超えています");
        int attempts = 0;
        byte[]? first = null;
        using var client = new HttpClient(new GitHubContractChecks.Handler((request, body) => {
            check(request.Method == HttpMethod.Post && request.Headers.Authorization == null && request.RequestUri!.Scheme == "https", "報告の送信先か認証が不正です");
            byte[] sent = Encoding.UTF8.GetBytes(body);
            if (first != null) check(first.SequenceEqual(sent), "再送信で受付番号か本文が変わりました");
            first = sent;
            return ++attempts == 1 ? new(HttpStatusCode.ServiceUnavailable) : GitHubContractChecks.JSON(JsonSerializer.Serialize(new BugReportReceipt(report.ID, "2026-11-01T00:00:00Z"), BugReports.Options));
        }));
        try { await BugReports.SendAsync(client, "https://unit.invalid/reports", report, default); throw new Exception("送信失敗を成功としました"); } catch (IOException) { check(true, "送信失敗"); }
        var receipt = await BugReports.SendAsync(client, "https://unit.invalid/reports", report, default);
        check(receipt.ID == report.ID && attempts == 2, "再送信の受付番号が一致しません");
        try { await BugReports.SendAsync(client, "http://unit.invalid/reports", report, default); throw new Exception("HTTPへ送信しました"); } catch (ArgumentException) { check(true, "HTTPS限定"); }
    }
}
