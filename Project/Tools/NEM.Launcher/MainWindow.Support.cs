using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using NEMEngine.Distribution;

namespace NEMEngine.Launcher;

public partial class MainWindow {
    private CancellationTokenSource? gitLinks_;
    internal static ProjectEntry? ProjectFromItem(object? item) => item switch { ProjectListEntry row => row.Project, ProjectEntry project => project, _ => null };

    private async void RefreshProjectGitLinks() {
        gitLinks_?.Cancel();
        gitLinks_?.Dispose();
        gitLinks_ = new();
        var token = gitLinks_.Token;
        if (Projects.ItemsSource is not ProjectListEntry[] rows) return;
        GitCommandRunner? runner = null;
        try {
            var tools = new GitToolchainService(store_.Root, client_).FindInstalled();
            if (tools != null) runner = new(tools, Environment.ProcessPath!, store_.Root);
        } catch (Exception error) when (error is IOException or System.Text.Json.JsonException or InvalidDataException) { }
        using var concurrency = new SemaphoreSlim(4);
        try {
            await Task.WhenAll(rows.Select(async row => {
                await concurrency.WaitAsync(token);
                try {
                    string url = await Task.Run(() => ProjectGitLink.ReadAsync(row.Project, store_.Root, runner, token), token);
                    if (!token.IsCancellationRequested) row.SetGitLink(url);
                } finally { concurrency.Release(); }
            }));
        } catch (OperationCanceledException) { }
    }

    private void OpenProjectGit(object sender, RoutedEventArgs e) {
        e.Handled = true;
        if (sender is Button { DataContext: ProjectListEntry row } && row.HasGitRepository) {
            string url = ProjectGitLink.Normalize(row.GitWebURL);
            if (url.Length != 0) OpenPath(url);
        }
    }

    private static bool IsButtonInteraction(DependencyObject source) {
        for (DependencyObject? current = source; current != null; current = current is Visual ? VisualTreeHelper.GetParent(current) : LogicalTreeHelper.GetParent(current))
            if (current is ButtonBase) return true;
        return false;
    }

    private void ReportBug(object sender, RoutedEventArgs e) {
        var project = ProjectFromItem(Projects.SelectedItem);
        var dialog = new BugReportDialog(this, store_, project);
        dialog.ShowDialog();
    }
}
