using System.Net;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;
using NEMEngine.Distribution;
using NEMEngine.Launcher;

internal static class BugReportDialogChecks {
    public static void Run(string root, Window owner, Action<bool, string> check) {
        var store = new LauncherStore(Path.Combine(root, "ReportFlow"));
        Directory.CreateDirectory(Path.Combine(store.Root, "Logs"));
        File.WriteAllText(Path.Combine(store.Root, "Logs/launcher.log"), "人工ログ");
        int requests = 0;
        string first = "";
        bool logsExpected = false;
        using var client = new HttpClient(new GitHubContractChecks.Handler((_, body) => {
            using var parsed = JsonDocument.Parse(body);
            var report = parsed.RootElement;
            check(report.GetProperty("attachLogs").GetBoolean() == logsExpected && report.GetProperty("logs").GetArrayLength() == (logsExpected ? 1 : 0), "画面の添付設定と送信内容が一致しません");
            if (++requests == 1) { first = body; return new(HttpStatusCode.ServiceUnavailable); }
            if (requests == 2) check(first == body, "画面からの再送信で内容が変わりました");
            return GitHubContractChecks.JSON(JsonSerializer.Serialize(new BugReportReceipt(report.GetProperty("id").GetString()!, "2026-11-01T00:00:00Z"), BugReports.Options));
        }));
        foreach (bool attach in new[] { false, true }) {
            logsExpected = attach;
            var dialog = new BugReportDialog(owner, store, null, client, "https://unit.invalid/reports") { ShowInTaskbar = false, ShowActivated = false };
            try {
                dialog.Show();
                dialog.UpdateLayout();
                var fields = Descendants<TextBox>(dialog).ToArray();
                fields[0].Text = "人工テスト";
                fields[1].Text = "人工の再現手順";
                Descendants<CheckBox>(dialog).Single().IsChecked = attach;
                var send = Descendants<Button>(dialog).Single(button => Equals(button.Content, "送信"));
                var copy = Descendants<Button>(dialog).Single(button => Equals(button.Content, "受付番号をコピー"));
                send.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                if (!attach) {
                    check(send.IsEnabled && fields[0].Text == "人工テスト" && !copy.IsEnabled, "失敗後に入力内容が失われました");
                    send.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                }
                var deadline = DateTime.UtcNow.AddSeconds(5);
                while (!copy.IsEnabled && DateTime.UtcNow < deadline) {
                    Dispatcher.CurrentDispatcher.Invoke(() => { }, DispatcherPriority.ApplicationIdle);
                    Thread.Sleep(10);
                }
                check(copy.IsEnabled && !send.IsEnabled, "受付成功後の表示へ切り替わりません");
            } finally { dialog.Close(); }
        }
        check(requests == 3, "報告画面の送信回数が一致しません");
    }

    private static IEnumerable<T> Descendants<T>(DependencyObject parent) where T : DependencyObject {
        for (int index = 0; index < VisualTreeHelper.GetChildrenCount(parent); ++index) {
            var child = VisualTreeHelper.GetChild(parent, index);
            if (child is T match) yield return match;
            foreach (var nested in Descendants<T>(child)) yield return nested;
        }
    }
}
