using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;
using NEMEngine.Distribution;

namespace NEMEngine.Launcher;

internal sealed class GitCloneDialog : LauncherDialog {
    private readonly TextBox url_ = new();
    private readonly TextBox parent_ = new();
    private readonly TextBox name_ = new();
    public string RepositoryURL => url_.Text.Trim();
    public string ParentDirectory => parent_.Text.Trim();
    public string FolderName => name_.Text.Trim();

    public GitCloneDialog(Window owner, string parent) : base(owner, "GitHubからプロジェクトを取得") {
        Description("招待された非公開プロジェクトは、先にGitHubで招待を受諾してください。所有者はGitHub Appの対象にリポジトリを追加しておきます。");
        Field("GitHubのリポジトリURL", url_);
        Field("保存先", parent_);
        parent_.Text = parent;
        var browse = new Button { Content = "保存先を選択" };
        browse.Click += (_, _) => {
            var dialog = new OpenFolderDialog { InitialDirectory = ParentDirectory };
            if (dialog.ShowDialog(this) == true) parent_.Text = dialog.FolderName;
        };
        Panel.Children.Add(browse);
        Field("ローカルのフォルダー名", name_);
        url_.LostFocus += (_, _) => {
            if (FolderName.Length != 0) return;
            try { name_.Text = GitHubRepositories.ParseURL(RepositoryURL).Split('/')[1]; }
            catch (ArgumentException) { }
        };
        var accept = new Button { Content = "取得して登録", IsDefault = true };
        accept.Click += (_, _) => {
            try {
                GitHubRepositories.ParseURL(RepositoryURL);
                ProjectService.ValidateName(FolderName);
                if (ParentDirectory.Length == 0) throw new ArgumentException("保存先を選択してください");
                DialogResult = true;
            } catch (ArgumentException error) { Error.Text = error.Message; }
        };
        Actions(accept, CancelButton());
    }
}
