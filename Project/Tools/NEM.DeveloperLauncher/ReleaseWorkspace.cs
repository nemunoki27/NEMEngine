using System.ComponentModel;
using System.Diagnostics;
using System.IO;

namespace NEMEngine.DeveloperLauncher;

internal sealed class ReleaseWorkspace(string repository) {
    private const string Marker = ".nem-developer-release";
    private readonly string root = Path.Combine(repository, "Generated", "Distribution", "DeveloperReleases");
    private string? current;

    internal string Create() {
        Discard();
        CheckParents();
        Directory.CreateDirectory(root);
        var path = Path.Combine(root, Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(path);
        current = path;
        try { SetWorker(null); } catch { current = null; throw; }
        return path;
    }

    internal void SetWorker(Process? worker) {
        if (current == null) return;
        using var owner = Process.GetCurrentProcess();
        string path = Path.Combine(current, Marker);
        string temp = path + ".writing";
        try {
            File.WriteAllLines(temp, [Path.GetFileName(current), Identity(owner), worker == null ? "" : Identity(worker)]);
            File.Move(temp, path, true);
        } finally { File.Delete(temp); }
    }

    internal void Discard() {
        if (current == null) return;
        CheckParents();
        if (Owned(current, out _)) Directory.Delete(current, true);
        current = null;
    }

    internal void RemoveAbandoned() {
        if (!Directory.Exists(root)) return;
        CheckParents();
        foreach (var directory in Directory.EnumerateDirectories(root)) {
            if (!Owned(directory, out var record)) continue;
            // 強制終了後もビルドや転送が続いている場合は次回起動時に回収する
            if (!Running(record[1]) && !Running(record[2])) Directory.Delete(directory, true);
        }
    }

    private static string Identity(Process process) => process.Id + "/" + process.StartTime.ToUniversalTime().Ticks;

    private static bool Running(string identity) {
        if (identity == "") return false;
        var parts = identity.Split('/');
        if (parts.Length != 2 || !int.TryParse(parts[0], out var id) || !long.TryParse(parts[1], out var ticks)) return true;
        try {
            using var process = Process.GetProcessById(id);
            return process.StartTime.ToUniversalTime().Ticks == ticks && !process.HasExited;
        } catch (ArgumentException) { return false; }
        catch (InvalidOperationException) { return false; }
        catch (Win32Exception) { return true; }
    }

    private static bool Owned(string directory, out string[] record) {
        record = [];
        string id = Path.GetFileName(directory);
        string marker = Path.Combine(directory, Marker);
        if (!Guid.TryParseExact(id, "N", out _) || !Directory.Exists(directory) || !File.Exists(marker) ||
            Linked(directory) || Linked(marker)) return false;
        record = File.ReadAllLines(marker);
        return record.Length == 3 && record[0] == id;
    }

    private static bool Linked(string path) => (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0;

    private void CheckParents() {
        for (var part = root; !string.Equals(part, repository, StringComparison.OrdinalIgnoreCase); part = Path.GetDirectoryName(part)!) {
            if (Directory.Exists(part) && Linked(part)) throw new IOException("配布作業フォルダーのリンク先は使用できません");
        }
    }
}
