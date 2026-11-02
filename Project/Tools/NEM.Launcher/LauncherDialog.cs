using System.Windows;
using System.Windows.Controls;

namespace NEMEngine.Launcher;

// 入力画面の余白と操作ボタンを揃える
internal class LauncherDialog : Window {
    protected readonly StackPanel Panel = new() { Margin = new Thickness(20) };
    protected readonly TextBlock Error = new() { Foreground = System.Windows.Media.Brushes.LightSalmon, TextWrapping = TextWrapping.Wrap, Margin = new(4, 8, 4, 0) };

    protected LauncherDialog(Window? owner, string title, double width = 580) {
        SetResourceReference(StyleProperty, "LauncherWindowStyle");
        if (owner != null) Owner = owner;
        Title = title;
        Width = width;
        SizeToContent = SizeToContent.Height;
        ResizeMode = ResizeMode.NoResize;
        WindowStartupLocation = owner == null ? WindowStartupLocation.CenterScreen : WindowStartupLocation.CenterOwner;
        MaxHeight = SystemParameters.WorkArea.Height - 40;
        Content = new ScrollViewer { Content = Panel, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled };
    }

    protected void Field(string label, UIElement element) {
        Panel.Children.Add(new TextBlock { Text = label, Margin = new(4, 10, 4, 2) });
        Panel.Children.Add(element);
    }

    protected void Description(string text) => Panel.Children.Add(new TextBlock { Text = text, TextWrapping = TextWrapping.Wrap, Margin = new(4, 4, 4, 10), Foreground = System.Windows.Media.Brushes.Silver });

    protected void Actions(params Button[] buttons) {
        Panel.Children.Add(Error);
        var grid = new System.Windows.Controls.Primitives.UniformGrid { Columns = buttons.Length, Margin = new(0, 12, 0, 0) };
        foreach (var button in buttons) grid.Children.Add(button);
        Panel.Children.Add(grid);
    }

    protected static Button CancelButton() => new() { Content = "キャンセル", IsCancel = true };

    internal static void OpenURL(string url) => System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(url) { UseShellExecute = true })?.Dispose();
}
