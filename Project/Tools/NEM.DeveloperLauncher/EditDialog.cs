using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Automation;
using Microsoft.Win32;

namespace NEMEngine.DeveloperLauncher;

internal sealed class EditDialog : Window {
    private readonly StackPanel fields = new();
    private readonly TextBlock error = new() { TextWrapping = TextWrapping.Wrap };
    private readonly Dictionary<string, TextBox> inputs = [];
    private Action? validate;

    internal EditDialog(Window owner, string title, string accept) {
        Owner = owner;
        Title = title;
        Width = 580;
        MaxHeight = Math.Min(760, SystemParameters.WorkArea.Height - 60);
        Style = (Style)FindResource("LauncherWindowStyle");
        SizeToContent = SizeToContent.Height;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        var panel = new DockPanel { Margin = new Thickness(18) };
        var footer = new StackPanel { Margin = new Thickness(0, 12, 0, 0) };
        DockPanel.SetDock(footer, Dock.Bottom);
        panel.Children.Add(footer);
        footer.Children.Add(error);
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right };
        footer.Children.Add(buttons);
        var cancel = new Button { Content = "キャンセル", IsCancel = true };
        var ok = new Button { Content = accept, IsDefault = true, MinWidth = 100 };
        ok.Click += (_, _) => {
            try { validate?.Invoke(); DialogResult = true; } catch (Exception e) { error.Text = e.Message; }
        };
        buttons.Children.Add(cancel);
        buttons.Children.Add(ok);
        panel.Children.Add(new ScrollViewer { Content = fields, VerticalScrollBarVisibility = ScrollBarVisibility.Auto });
        Content = panel;
    }

    internal void Label(string value) => fields.Children.Add(new TextBlock { Text = value, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(4, 9, 4, 2) });
    internal void Input(string key, string label, string value, bool multiline = false) {
        Label(label);
        var box = new TextBox { Text = value, AcceptsReturn = multiline, TextWrapping = multiline ? TextWrapping.Wrap : TextWrapping.NoWrap };
        if (multiline) { box.Height = 85; box.VerticalScrollBarVisibility = ScrollBarVisibility.Auto; }
        box.MaxLength = key switch { "notes" or "description" => 4000, "name" => 120, _ => 0 };
        AutomationProperties.SetName(box, label);
        AutomationProperties.SetAutomationId(box, key);
        inputs.Add(key, box);
        fields.Children.Add(box);
    }
    internal string Value(string key) => inputs[key].Text.Trim();

    internal void Files(string projectRoot, IEnumerable<string> paths) {
        Input("paths", "追加ファイル・フォルダー（プロジェクト相対パス、1行に1件）", string.Join(Environment.NewLine, paths), true);
        var row = new WrapPanel();
        fields.Children.Add(row);
        void AddPath(string path) {
            var relative = Path.GetRelativePath(projectRoot, path).Replace('\\', '/');
            if (relative == "." || relative == ".." || relative.StartsWith("../") || Path.IsPathRooted(relative)) {
                error.Text = "保存元プロジェクト内を選択してください";
                return;
            }
            var items = Paths().Append(relative).Distinct(StringComparer.OrdinalIgnoreCase);
            inputs["paths"].Text = string.Join(Environment.NewLine, items);
        }
        var file = new Button { Content = "ファイルを追加" };
        var folder = new Button { Content = "フォルダーを追加" };
        row.Children.Add(file);
        row.Children.Add(folder);
        file.Click += (_, _) => {
            var dialog = new OpenFileDialog { Multiselect = true, InitialDirectory = projectRoot };
            if (dialog.ShowDialog(this) == true)
            foreach (var path in dialog.FileNames)
                AddPath(path);
        };
        folder.Click += (_, _) => {
            var dialog = new OpenFolderDialog { InitialDirectory = projectRoot };
            if (dialog.ShowDialog(this) == true)
                AddPath(dialog.FolderName);
        };
    }

    internal string[] Paths() => Value("paths").Split(['\r', '\n'], StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries)
        .Distinct(StringComparer.OrdinalIgnoreCase).ToArray();

    internal void Zip(string value) {
        Input("zip", "配布ZIP", value);
        var button = new Button { Content = "ZIPを選択", HorizontalAlignment = HorizontalAlignment.Left };
        fields.Children.Add(button);
        button.Click += (_, _) => {
            var dialog = new OpenFileDialog { Filter = "ZIP (*.zip)|*.zip" };
            if (dialog.ShowDialog(this) == true)
                inputs["zip"].Text = dialog.FileName;
        };
    }

    internal bool Accept(Action validation) { validate = validation; return ShowDialog() == true; }
}
