using System.Text;

namespace NEM.ComponentBindingGen;

// 生成した成果物の保存と復元を行う
internal static class BindingArtifactStore {

    // 全成果物を用意してから差し替え、途中失敗では戻す
    internal static void WriteAll(IReadOnlyList<(string path, string text)> outputs) {
        var staged = new List<(string path, string temporary, byte[]? previous)>();
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var recoveryFiles = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        int published = 0;
        try {
            foreach (var output in outputs) {
                string path = Path.GetFullPath(output.path);
                if (!paths.Add(path)) { throw new InvalidOperationException($"Duplicate output: {path}"); }
                byte[]? previous = File.Exists(path) ? File.ReadAllBytes(path) : null;
                if (previous != null && Encoding.UTF8.GetString(previous).Replace("\r\n", "\n") == output.text) { continue; }
                string temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
                staged.Add((path, temporary, previous));
                File.WriteAllText(temporary, output.text, new UTF8Encoding(false));
            }
            foreach (var output in staged) {
                File.Move(output.temporary, output.path, overwrite: true);
                ++published;
            }
        }
        catch (Exception error) {
            var failures = new List<Exception> { error };
            // 差し替え済みの成果物を逆順に復元する
            for (int i = published - 1; i >= 0; --i) {
                var output = staged[i];
                try {
                    if (output.previous == null) { File.Delete(output.path); }
                    else {
                        File.WriteAllBytes(output.temporary, output.previous);
                        File.Move(output.temporary, output.path, overwrite: true);
                    }
                }
                catch (Exception rollback) {
                    recoveryFiles.Add(output.temporary);
                    failures.Add(rollback);
                    Console.Error.WriteLine($"[ComponentBindingGen] rollback failed; recovery file: {output.temporary}");
                }
            }
            throw new AggregateException("Binding output publication failed.", failures);
        }
        finally {
            foreach (var output in staged) {
                if (recoveryFiles.Contains(output.temporary)) { continue; }
                try { File.Delete(output.temporary); }
                catch (Exception ex) { Console.Error.WriteLine($"[ComponentBindingGen] temporary cleanup failed: {output.temporary}: {ex.Message}"); }
            }
        }
        foreach (var output in staged) { Console.WriteLine($"[ComponentBindingGen] wrote {Path.GetFileName(output.path)}"); }
    }

    // 保存済みの成果物と生成結果の差分を確認する
    internal static void CheckDrift(string path, string expected, Action<string> fail) {
        if (!File.Exists(path)) {
            fail($"generated file missing (drift): {path}");
            return;
        }
        string current = File.ReadAllText(path).Replace("\r\n", "\n");
        if (current != expected) {
            fail($"generated file drifted from schema: {Path.GetFileName(path)} (re-run generator).");
        }
    }
}
