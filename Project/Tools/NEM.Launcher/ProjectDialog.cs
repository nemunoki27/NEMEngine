using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;

namespace NEMEngine.Launcher;

// 作成と使用版変更の入力をまとめる
internal sealed class ProjectDialog : Window {
    private readonly TextBox name_ = new();
    private readonly TextBox parent_ = new();
    private readonly ComboBox version_ = new();
    private readonly ComboBox template_ = new();
    public string ProjectName => name_.Text.Trim();
    public string ParentDirectory => parent_.Text;
    public string EngineVersion => (string)version_.SelectedItem;
    public string ProjectTemplate => (string)template_.SelectedItem;

    public ProjectDialog(Window owner, IEnumerable<string> versions, string parent, string? current = null) {
        SetResourceReference(StyleProperty, "LauncherWindowStyle");
        Owner = owner;
        Title = current == null ? "プロジェクトを作成" : "エンジンバージョンの変更";
        Width = 540;
        SizeToContent = SizeToContent.Height;
        ResizeMode = ResizeMode.NoResize;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        var panel = new StackPanel { Margin = new Thickness(20) };
        Content = panel;
        version_.ItemsSource = versions.OrderByDescending(Version.Parse).ToArray();
        version_.SelectedItem = current;
        if (version_.SelectedItem == null) version_.SelectedIndex = 0;
        template_.ItemsSource = new[] { "3D", "2D" };
        template_.SelectedIndex = 0;
        parent_.Text = parent;
        if (current == null) {
            AddField(panel, "プロジェクト名", name_);
            var destination = new DockPanel();
            var select = new Button { Content = "選択" };
            select.Click += (_, _) => {
                var dialog = new OpenFolderDialog { InitialDirectory = parent_.Text };
                if (dialog.ShowDialog(this) == true) parent_.Text = dialog.FolderName;
            };
            DockPanel.SetDock(select, Dock.Right);
            destination.Children.Add(select);
            destination.Children.Add(parent_);
            AddField(panel, "保存先", destination);
            AddField(panel, "テンプレート", template_);
        }
        AddField(panel, "エンジンのバージョン", version_);
        var accept = new Button { Content = current == null ? "作成" : "変更", Margin = new(0, 18, 0, 0), IsDefault = true };
        accept.Click += (_, _) => {
            if (version_.SelectedItem == null || (current == null && ProjectName.Length == 0)) return;
            DialogResult = true;
        };
        panel.Children.Add(accept);
    }

    private static void AddField(Panel panel, string label, UIElement input) {
        panel.Children.Add(new TextBlock { Text = label, Margin = new(0, 8, 0, 4) });
        panel.Children.Add(input);
    }
}
