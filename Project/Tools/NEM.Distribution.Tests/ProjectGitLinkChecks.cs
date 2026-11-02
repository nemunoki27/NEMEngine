using NEMEngine.Distribution;

internal static class ProjectGitLinkChecks {
    public static async Task RunAsync(string root, Action<bool, string> check) {
        foreach (string url in new[] { "https://github.com/owner/project.git", "git@github.com:owner/project.git", "ssh://git@github.com/owner/project.git", "ssh://git@ssh.github.com:443/owner/project.git", "https://user:password@github.com/owner/project.git" })
            check(ProjectGitLink.Normalize(url) == "https://github.com/owner/project", "GitHub URLの形式を正規化できません: " + url);
        foreach (string url in new[] { "https://example.com/owner/project", "http://github.com/owner/project", "https://github.com/owner/project?token=x", "https://github.com/owner/project#fragment", "file:///C:/repo" })
            check(ProjectGitLink.Normalize(url).Length == 0, "不正なGitHub URLをリンクにしました");
        string directory = Path.Combine(root, "GitLinkProject");
        Directory.CreateDirectory(Path.Combine(directory, ".git"));
        File.WriteAllText(Path.Combine(directory, ".git/config"), "[remote \"origin\"]\n\turl = git@github.com:owner/project.git\n");
        var project = new ProjectEntry { Descriptor = Path.Combine(directory, "Project.nemproject") };
        check(await ProjectGitLink.ReadAsync(project, root, null, default) == "https://github.com/owner/project", "Git設定からリンクを読み取れません");
        var child = project with { Descriptor = Path.Combine(directory, "Child/Project.nemproject") };
        check(await ProjectGitLink.ReadAsync(child, root, null, default) == "", "親フォルダーのリポジトリを流用しました");
        string worktree = Path.Combine(root, "GitLinkWorktree");
        Directory.CreateDirectory(worktree);
        Directory.CreateDirectory(Path.Combine(directory, ".git/worktrees/second"));
        File.WriteAllText(Path.Combine(directory, ".git/worktrees/second/commondir"), "../..");
        File.WriteAllText(Path.Combine(worktree, ".git"), "gitdir: " + Path.Combine(directory, ".git/worktrees/second"));
        check(await ProjectGitLink.ReadAsync(project with { Descriptor = Path.Combine(worktree, "Project.nemproject") }, root, null, default) == "https://github.com/owner/project", "worktreeの共有設定を読めません");
    }
}
