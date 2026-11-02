using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using NEMEngine.Distribution;
using NEMEngine.Launcher;

internal static class LauncherGitChecks {
    public static void Run(string repository, string root, Window owner, Action<bool, string> check) {
        var project = new ProjectEntry { Name = "TeamProject", Descriptor = Path.Combine(repository, "Generated/TeamProject.nemproject") };
        var rename = new ProjectRenameDialog(owner, project);
        Capture(rename, repository, "Rename", check);
        check(rename.ProjectName == project.Name, "名前変更の初期値がありません");
        check(!Descendants<TextBlock>(rename).Any(label => label.Text.Contains("フォルダーとプロジェクトファイル")), "名前変更の旧説明が残っています");
        var version = new ProjectDialog(owner, new[] { "0.1.3" }, "", "0.1.3");
        Capture(version, repository, "ChangeEngine", check);
        check(Descendants<Button>(version).Any(button => Equals(button.Content, "変更")), "バージョン変更のボタン名が旧表記です");
        var clone = new GitCloneDialog(owner, Path.Combine(repository, "Generated"));
        Capture(clone, repository, "Clone", check);
        int requests = 0;
        using var client = new HttpClient(new GitHubContractChecks.Handler((request, _) => {
            ++requests;
            int id = request.Headers.Authorization?.Parameter == "ui-2" ? 2 : 1;
            return GitHubContractChecks.JSON($"{{\"installations\":[{{\"account\":{{\"id\":{id},\"login\":\"{(id == 1 ? "example" : "second")}\",\"type\":\"User\"}}}}]}}");
        }));
        string sessionRoot = Path.Combine(root, "DestinationUI");
        DistributionStorage.Write(Path.Combine(sessionRoot, "github-session.json"), new GitHubAccounts { ActiveAccountID = 1,
            Accounts = [new(1, "example", "Example"), new(2, "second", "Second")] });
        var credentials = new GitHubContractChecks.MemoryCredentials();
        foreach (int id in new[] { 1, 2 }) credentials.Write(DistributionDefaults.GitHub.ClientID + "/" + id, new("ui-" + id, "", DateTimeOffset.MaxValue));
        var session = new GitHubSession(sessionRoot, client, DistributionDefaults.GitHub, credentials);
        var gateway = new GitHubRepositories(client, session);
        var create = new GitRepositoryDialog(owner, project, new("", new[] { new GitUploadFile("GameAssets/Texture.png", 1024, true, false) }, "main"),
            new[] { new GitHubRepositoryDestination(1, "example", new(1, "example", false, true)),
                new GitHubRepositoryDestination(2, "second", new(2, "second", false, true)) }, 1, gateway, DistributionDefaults.GitHub);
        Capture(create, repository, "CreateRepository", check);
        check(create.IsPrivate && create.RepositoryName == project.Name && create.RepositoryOwner.Login == "example",
            "非公開の初期値か作成先が正しくありません");
        check(Descendants<TextBlock>(create).Any(label => label.Text == "example（個人）"), "GitHubの作成先が利用者向けの名前で表示されません");
        check(Descendants<ComboBox>(create).Any(combo => combo.Items.Cast<object>().SequenceEqual(new[] { "Private", "Public" })), "公開範囲に説明が残っています");
        check(!Descendants<TextBlock>(create).Any(label => label.Text.StartsWith("下のファイルを初回commit")), "作成画面の旧説明が残っています");
        check(Descendants<Button>(create).Any(button => Equals(button.Content, "作成")), "Git作成のボタン名が揃っていません");
        var ownerList = Descendants<ComboBox>(create).Single(combo => combo.Items.Count != 0 && combo.Items[0] is GitHubRepositoryDestination);
        check(ownerList.Items.Count == 2 && create.AccountID == 1, "既定以外のアカウントが作成先にありません");
        create.Show();
        create.UpdateLayout();
        ownerList.IsDropDownOpen = true;
        create.UpdateLayout();
        System.Windows.Threading.Dispatcher.CurrentDispatcher.Invoke(() => { }, System.Windows.Threading.DispatcherPriority.ApplicationIdle);
        var popup = (Popup)ownerList.Template.FindName("PART_Popup", ownerList);
        check(Descendants<TextBlock>(popup.Child).Any(label => label.Text == "second（個人）"), "ドロップダウンに追加アカウントが描画されません");
        SaveCapture((FrameworkElement)popup.Child, repository, "CreateRepository.Destinations");
        ownerList.IsDropDownOpen = false;
        ownerList.SelectedIndex = 1;
        check(create.AccountID == 2 && create.RepositoryOwner.ID == 2, "作成先の選択と認証アカウントが一致しません");
        Descendants<Button>(create).Single(button => Equals(button.Content, "作成先を更新")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        check(requests == 2 && create.AccountID == 2, "一覧更新で選択した別アカウントを失いました");
        Capture(create, repository, "CreateRepository.SecondAccount", check);
        DistributionStorage.Write(Path.Combine(sessionRoot, "github-session.json"), new GitHubAccounts { ActiveAccountID = 1, Accounts = [new(1, "example", "Example")] });
        Descendants<Button>(create).Single(button => Equals(button.Content, "作成先を更新")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        check(ownerList.SelectedItem == null, "消えた作成先を既定アカウントへ自動で切り替えました");
        var remote = new GitHubRepository(2, "example", "game", true, true, true);
        var manage = new GitRepositoryManageDialog(owner, new() { Repository = remote }, remote);
        Capture(manage, repository, "DeleteRepository", check);
        var buttons = Descendants<Button>(manage).ToArray();
        check(buttons.Single(button => Equals(button.Content, "GitHub上のリポジトリを削除")).IsEnabled == false,
            "名前の確認前にGitHubの削除を実行できます");
        check(!Descendants<TextBlock>(manage).Any(label => label.Text.Contains("GitHub上の共有先を")), "削除画面の旧説明が残っています");
        var choices = new[] { new GitHubAccountChoice(new(7, "first", "First")), new GitHubAccountChoice(new(8, "second", "Second")) };
        var changeAccount = new GitRepositoryManageDialog(owner, new() { Repository = remote, AccountID = 7, UploadComplete = true }, remote, choices);
        Capture(changeAccount, repository, "ProjectAccount", check);
        var select = Descendants<ComboBox>(changeAccount).Single();
        check(changeAccount.AccountID == 7, "プロジェクトに設定したアカウントを選択していません");
        select.SelectedIndex = 1;
        check(changeAccount.AccountID == 8 && Descendants<Button>(changeAccount).Single(button => Equals(button.Content, "変更")).IsEnabled,
            "既存プロジェクトの使用アカウントを選択できません");
        foreach (var dialog in new Window[] { rename, version, clone, create, manage, changeAccount }) dialog.Close();
    }

    private static void Capture(Window window, string repository, string name, Action<bool, string> check) {
        window.ShowInTaskbar = false;
        window.ShowActivated = false;
        window.Show();
        window.UpdateLayout();
        System.Windows.Threading.Dispatcher.CurrentDispatcher.Invoke(() => { }, System.Windows.Threading.DispatcherPriority.ApplicationIdle);
        window.UpdateLayout();
        check(window.Background is SolidColorBrush brush && brush.Color.R < 96 && window.ActualHeight <= SystemParameters.WorkArea.Height,
            "Git操作画面の背景か高さが正しくありません");
        SaveCapture(window, repository, name);
        window.Hide();
    }

    private static void SaveCapture(FrameworkElement element, string repository, string name) {
        var bitmap = new RenderTargetBitmap((int)Math.Ceiling(element.ActualWidth), (int)Math.Ceiling(element.ActualHeight), 96, 96, PixelFormats.Pbgra32);
        bitmap.Render(element);
        var encoder = new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create(bitmap));
        using var stream = File.Create(Path.Combine(repository, "Generated/Launcher." + DistributionVersion.Current + "." + name + ".png"));
        encoder.Save(stream);
    }

    private static IEnumerable<T> Descendants<T>(DependencyObject parent) where T : DependencyObject {
        for (int index = 0; index < VisualTreeHelper.GetChildrenCount(parent); ++index) {
            var child = VisualTreeHelper.GetChild(parent, index);
            if (child is T match) yield return match;
            foreach (var nested in Descendants<T>(child)) yield return nested;
        }
    }
}
