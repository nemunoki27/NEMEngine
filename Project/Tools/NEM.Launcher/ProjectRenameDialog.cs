using System.IO;
using System.Windows;
using System.Windows.Controls;
using NEMEngine.Distribution;

namespace NEMEngine.Launcher;

internal sealed class ProjectRenameDialog : LauncherDialog {
    private readonly TextBox name_ = new();
    public string ProjectName => name_.Text.Trim();

    public ProjectRenameDialog(Window owner, ProjectEntry project) : base(owner, "プロジェクトの名前変更") {
        Description("EditorとVisual StudioのSolutionを閉じてから変更してください。");
        name_.Text = project.Name;
        Field("新しいプロジェクト名", name_);
        var preview = new TextBlock { TextWrapping = TextWrapping.Wrap, Foreground = System.Windows.Media.Brushes.Silver, Margin = new(4, 8, 4, 4) };
        Panel.Children.Add(preview);
        void UpdatePreview() {
            try {
                ProjectService.ValidateName(ProjectName);
                preview.Text = "保存先: " + Path.Combine(Path.GetDirectoryName(Path.GetDirectoryName(project.Descriptor))!, ProjectName, ProjectName + ".nemproject");
            } catch (ArgumentException) { preview.Text = "新しいプロジェクト名を入力してください"; }
        }
        name_.TextChanged += (_, _) => UpdatePreview();
        UpdatePreview();
        var accept = new Button { Content = "名前を変更", IsDefault = true };
        accept.Click += (_, _) => {
            try { ProjectService.ValidateName(ProjectName); DialogResult = true; }
            catch (ArgumentException error) { Error.Text = error.Message; }
        };
        Actions(accept, CancelButton());
        Loaded += (_, _) => { name_.SelectAll(); name_.Focus(); };
    }
}
