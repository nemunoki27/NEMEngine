using System.IO;
using System.Net.Http;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using NEMEngine.Distribution;

namespace NEMEngine.Launcher;

internal sealed class BugReportDialog : LauncherDialog {
    private sealed record ProjectChoice(string Name, ProjectEntry? Project);
    private readonly TextBox title_ = new() { MaxLength = 200 };
    private readonly TextBox description_ = new() { MaxLength = 16000, AcceptsReturn = true, TextWrapping = TextWrapping.Wrap,
        VerticalScrollBarVisibility = ScrollBarVisibility.Auto, Height = 150 };
    private readonly TextBox contact_ = new() { MaxLength = 256 };
    private readonly ComboBox kind_ = new() { ItemsSource = new[] { "ランチャー", "エンジン" }, SelectedIndex = 0 };
    private readonly ComboBox project_ = new() { DisplayMemberPath = "Name" };
    private readonly CheckBox attach_ = new() { Content = "ログファイルを添付", IsChecked = false, Margin = new(4,12,4,4) };
    private readonly TextBlock files_ = new() { Margin = new(4,4,4,4), Foreground = Brushes.Silver, TextWrapping = TextWrapping.Wrap };
    private readonly StackPanel fields_ = new();
    private readonly CancellationTokenSource stop_ = new();
    private readonly HttpClient client_;
    private readonly LauncherStore store_;
    private readonly string url_;
    private BugReportRequest? frozen_;
    private bool closed_;
    private string receipt_ = "";
    internal bool AttachesLogs => attach_.IsChecked == true;

    public BugReportDialog(Window owner, LauncherStore store, ProjectEntry? selected, HttpClient? client = null, string? url = null)
        : base(owner, "バグ報告", 720) {
        store_ = store;
        client_ = client ?? new(new HttpClientHandler { AllowAutoRedirect = false }) { Timeout = TimeSpan.FromSeconds(30) };
        url_ = url ?? BugReports.DefaultURL;
        var choices = new[] { new ProjectChoice("なし", null) }.Concat(store.State.Projects.OrderBy(item => item.Name).Select(item => new ProjectChoice(item.Name, item))).ToArray();
        project_.ItemsSource = choices;
        project_.SelectedItem = choices.FirstOrDefault(item => item.Project?.Descriptor == selected?.Descriptor) ?? choices[0];
        kind_.SelectedIndex = selected == null ? 0 : 1;
        Panel.Children.Add(fields_);
        AddField("種類", kind_);
        AddField("対象プロジェクト", project_);
        AddField("タイトル", title_);
        AddField("現象・再現手順", description_);
        AddField("連絡先（任意）", contact_);
        fields_.Children.Add(attach_);
        fields_.Children.Add(files_);
        fields_.Children.Add(new TextBlock { Text = "送信内容は開発者のみが確認し、30日後に削除します。", Foreground = Brushes.Silver, TextWrapping = TextWrapping.Wrap, Margin = new(4,8,4,0) });
        foreach (var text in new[] { title_, description_, contact_ }) text.TextChanged += (_, _) => Invalidate();
        kind_.SelectionChanged += (_, _) => Invalidate();
        project_.SelectionChanged += (_, _) => { Invalidate(); UpdateFiles(); };
        attach_.Checked += (_, _) => { Invalidate(); UpdateFiles(); };
        attach_.Unchecked += (_, _) => { Invalidate(); files_.Text = ""; };
        var send = new Button { Content = "送信", IsDefault = true };
        var copy = new Button { Content = "受付番号をコピー", IsEnabled = false };
        var close = CancelButton();
        copy.Click += (_, _) => Clipboard.SetText(receipt_);
        send.Click += async (_, _) => {
            fields_.IsEnabled = send.IsEnabled = close.IsEnabled = false;
            Error.Foreground = Brushes.LightSalmon;
            Error.Text = "送信中";
            try {
                var project = (project_.SelectedItem as ProjectChoice)?.Project;
                if (frozen_ == null) {
                    var request = new BugReportRequest { Title = title_.Text.Trim(), Description = description_.Text.Trim(), Contact = contact_.Text.Trim(),
                        Kind = kind_.SelectedIndex == 0 ? "Launcher" : "Engine", ProjectName = project?.Name ?? "", EngineVersion = project?.EngineVersion ?? "", AttachLogs = AttachesLogs };
                    BugReports.Serialize(request);
                    if (request.AttachLogs) {
                        string[] paths = new[] { Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), project == null ? "" : Path.GetDirectoryName(project.Descriptor)!, AppContext.BaseDirectory }
                            .Concat(store.State.Engines.Select(item => item.Root)).ToArray();
                        request = request with { Logs = await Task.Run(() => BugReportLogs.Capture(store.Root, project, paths, stop_.Token), stop_.Token) };
                    }
                    frozen_ = request;
                }
                var result = await BugReports.SendAsync(client_, url_, frozen_, stop_.Token);
                if (closed_) return;
                receipt_ = result.ID;
                Error.Foreground = Brushes.LightGreen;
                Error.Text = "送信しました\n受付番号: " + receipt_;
                copy.IsEnabled = true;
                close.Content = "閉じる";
            } catch (OperationCanceledException) { if (!closed_) Error.Text = "送信結果を確認できません 同じ内容で再試行してください"; }
            catch (Exception error) { if (!closed_) Error.Text = error.Message; }
            finally {
                if (!closed_) { fields_.IsEnabled = send.IsEnabled = receipt_.Length == 0; close.IsEnabled = true; }
            }
        };
        Actions(send, copy, close);
        Closing += (_, e) => { if (!send.IsEnabled && receipt_.Length == 0) stop_.Cancel(); };
        Closed += (_, _) => { closed_ = true; stop_.Cancel(); stop_.Dispose(); if (client == null) client_.Dispose(); };
    }

    private void AddField(string label, UIElement element) {
        fields_.Children.Add(new TextBlock { Text = label, Margin = new(4,8,4,2) });
        fields_.Children.Add(element);
    }

    private void Invalidate() { frozen_ = null; Error.Text = ""; }

    private void UpdateFiles() {
        if (!AttachesLogs) return;
        var project = (project_.SelectedItem as ProjectChoice)?.Project;
        files_.Text = string.Join('\n', BugReportLogs.Describe(store_.Root, project).Select(file => file.Name + " / " +
            (file.Status.Length != 0 ? file.Status : file.Bytes > BugReportLogs.MaxLogBytes ? "先頭と末尾を最大1MiB添付" : $"{file.Bytes / 1024.0:F1} KiB")));
    }
}
