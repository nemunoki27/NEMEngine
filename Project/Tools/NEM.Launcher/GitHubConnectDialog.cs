using System.Windows;
using System.Windows.Controls;
using NEMEngine.Distribution;

namespace NEMEngine.Launcher;

internal sealed class GitHubConnectDialog : LauncherDialog {
    private readonly CancellationTokenSource stop_ = new();
    private readonly TextBlock code_ = new() { FontSize = 26, FontWeight = FontWeights.SemiBold, Margin = new(4, 10, 4, 10) };
    private readonly Button browser_ = new() { Content = "ブラウザーで認証", IsEnabled = false };
    private readonly Button copy_ = new() { Content = "コードをコピー", IsEnabled = false };
    private bool closed_;

    public GitHubConnectDialog(Window? owner, GitHubSession session) : base(owner, "GitHubへ接続") {
        Description("ブラウザーで自分のGitHubアカウントへログインし、下の認証コードを入力してください。パスワードをランチャーへ入力する必要はありません。");
        Panel.Children.Add(code_);
        browser_.Click += (_, _) => OpenURL("https://github.com/login/device");
        copy_.Click += (_, _) => Clipboard.SetText(code_.Text);
        Actions(browser_, copy_, CancelButton());
        Closed += (_, _) => { closed_ = true; stop_.Cancel(); };
        Loaded += async (_, _) => {
            try {
                code_.Text = "認証コードを取得中";
                await session.ConnectAsync(value => {
                    code_.Text = value.UserCode;
                    browser_.IsEnabled = copy_.IsEnabled = true;
                    OpenURL(value.VerificationURL);
                }, stop_.Token);
                if (!closed_) DialogResult = true;
            } catch (OperationCanceledException) { if (!closed_) Error.Text = "接続をキャンセルしました"; }
            catch (Exception error) { if (!closed_) Error.Text = error.Message; }
        };
    }
}
