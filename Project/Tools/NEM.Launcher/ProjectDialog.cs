using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;
using NEMEngine.Distribution;

namespace NEMEngine.Launcher;

// 作成と使用版変更の入力をまとめる
internal sealed class ProjectDialog : Window {
    private readonly TextBox name_ = new();
    private readonly TextBox parent_ = new();
    private readonly ComboBox version_ = new();
    private readonly ComboBox template_ = new();
    private readonly TextBlock templateDetail_ = new() { TextWrapping = TextWrapping.Wrap, Margin = new(0, 6, 0, 0) };
    private readonly SampleRelease[] samples_;
    public string ProjectName => name_.Text.Trim();
    public string ParentDirectory => parent_.Text;
    public string EngineVersion => (string)version_.SelectedItem;
    public string ProjectTemplate => (template_.SelectedItem as TemplateChoice)?.ID ?? "3D";
    public SampleRelease? Sample => (template_.SelectedItem as TemplateChoice)?.Sample;
    internal ComboBox TemplateSelector => template_;
    internal ComboBox VersionSelector => version_;

    internal sealed record TemplateChoice(string ID, string Label, SampleRelease? Sample = null) {
        public override string ToString() => Label;
    }

    public ProjectDialog(Window owner, IEnumerable<string> versions, string parent, string? current = null,
        IEnumerable<SampleRelease>? samples = null) {
        SetResourceReference(StyleProperty, "LauncherWindowStyle");
        Owner = owner;
        Title = current == null ? "プロジェクトを作成" : "エンジンバージョンの変更";
        Width = 600;
        SizeToContent = SizeToContent.Height;
        ResizeMode = ResizeMode.NoResize;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        var panel = new StackPanel { Margin = new Thickness(20) };
        Content = panel;
        samples_ = samples?.ToArray() ?? [];
        version_.ItemsSource = versions.OrderByDescending(Version.Parse).ToArray();
        version_.SelectedItem = current;
        if (version_.SelectedItem == null) version_.SelectedIndex = 0;
        version_.SelectionChanged += (_, _) => RefreshTemplates();
        template_.SelectionChanged += (_, _) => {
            templateDetail_.Text = Sample is { } sample
                ? $"{sample.Description}\n版 {sample.Version} / {sample.Package.Bytes / 1048576.0:F1} MB"
                : "";
        };
        RefreshTemplates();
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
        }
        AddField(panel, "エンジンのバージョン", version_);
        if (current == null) {
            AddField(panel, "テンプレート", template_);
            panel.Children.Add(templateDetail_);
        }
        var accept = new Button { Content = current == null ? "作成" : "変更", Margin = new(0, 18, 0, 0), IsDefault = true };
        accept.Click += (_, _) => {
            if (version_.SelectedItem == null || (current == null && ProjectName.Length == 0)) return;
            DialogResult = true;
        };
        panel.Children.Add(accept);
    }

    private void RefreshTemplates() {
        string selected = ProjectTemplate;
        template_.Items.Clear();
        template_.Items.Add(new TemplateChoice("2D", "2D"));
        template_.Items.Add(new TemplateChoice("3D", "3D"));
        template_.Items.Add(new ComboBoxItem { Content = new Separator(), IsEnabled = false, Padding = new(0) });
        template_.Items.Add(new ComboBoxItem { Content = "サンプルプロジェクト", IsEnabled = false });
        var available = samples_.Where(sample => sample.EngineVersion == (string?)version_.SelectedItem)
            .GroupBy(sample => sample.ID).Select(group => group.MaxBy(sample => Version.Parse(sample.Version))!)
            .OrderBy(sample => sample.Name, StringComparer.CurrentCulture).ToArray();
        foreach (var sample in available) template_.Items.Add(new TemplateChoice(sample.ID, sample.Name, sample));
        if (available.Length == 0) template_.Items.Add(new ComboBoxItem { Content = "この版のサンプルはありません", IsEnabled = false });
        template_.SelectedItem = template_.Items.OfType<TemplateChoice>().FirstOrDefault(choice => choice.ID == selected)
            ?? template_.Items.OfType<TemplateChoice>().Single(choice => choice.ID == "3D");
    }

    private static void AddField(Panel panel, string label, UIElement input) {
        panel.Children.Add(new TextBlock { Text = label, Margin = new(0, 8, 0, 4) });
        panel.Children.Add(input);
    }
}
