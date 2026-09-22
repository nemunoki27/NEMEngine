using System.Text;
using System.Text.Json;
using static NEM.ComponentBindingGen.BindingTypeLayout;
using static NEM.ComponentBindingGen.BindingOutputText;
using static NEM.ComponentBindingGen.NativeBindingEmitter;
using static NEM.ComponentBindingGen.ManagedBindingEmitter;
using static NEM.ComponentBindingGen.BindingArtifactStore;

namespace NEM.ComponentBindingGen;

// Component連携の生成処理
internal static class BindingArtifactStore {

    internal static void WriteIfChanged(string path, string content) {
        string? current = File.Exists(path) ? File.ReadAllText(path) : null;
        if (current != null && current.Replace("\r\n", "\n") == content) {
            Console.WriteLine($"[ComponentBindingGen] up-to-date {Path.GetFileName(path)}");
            return;
        }
        File.WriteAllText(path, content, new UTF8Encoding(false));
        Console.WriteLine($"[ComponentBindingGen] wrote {Path.GetFileName(path)}");
    }

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
