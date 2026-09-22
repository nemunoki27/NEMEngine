using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace NEM.ScriptMetaSync;

// meta同期の順序と終了結果を調停する
internal static class Program {

    private static int Main(string[] args) {

        var diagnostics = new ScriptMetaDiagnostics();
        var planner = new ScriptMetaPlanner(diagnostics);
        var storage = new ScriptMetaStorage(diagnostics);
        string? root = null;
        Mode mode = Mode.EditorSync;

        for (int i = 0; i < args.Length; ++i) {
            switch (args[i]) {
                case "--root": if (i + 1 < args.Length) { root = args[++i]; } break;
                case "--mode": if (i + 1 < args.Length) { mode = ParseMode(args[++i]); } break;
            }
        }

        // モードは環境変数でも上書きできる（CI: NEMScriptMetadataMode=ValidateOnly）
        string? envMode = Environment.GetEnvironmentVariable("NEMScriptMetadataMode");
        if (!string.IsNullOrWhiteSpace(envMode)) {
            mode = ParseMode(envMode);
        }

        if (string.IsNullOrWhiteSpace(root) || !Directory.Exists(root)) {
            Console.Error.WriteLine($"[ScriptMetaSync] script root not found: {root}");
            return 1;
        }

        Console.WriteLine($"[ScriptMetaSync] mode={mode} root={root}");

        var perFile = ScriptSourceCollector.Collect(root);

        // GUID の正規化・重複検出（scriptTypeID は全体一意、fieldID は型内一意）
        var seenScriptIDs = new Dictionary<string, string>(StringComparer.Ordinal);

        bool anyChange = false;
        foreach ((string file, List<ScriptModel> models) in perFile) {
            JsonObject meta = storage.LoadMeta(file + ".meta");
            JsonObject planned = planner.Plan(file, meta, models, mode, seenScriptIDs);
            anyChange |= storage.WriteMetaIfChanged(file + ".meta", planned, mode);
        }

        if (diagnostics.errorCount > 0) {
            Console.Error.WriteLine($"[ScriptMetaSync] failed with {diagnostics.errorCount} error(s).");
            return 1;
        }
        if (diagnostics.ambiguousCount > 0) {
            // 曖昧 rename は build/reload を止めない（非破壊で新規 UUID を採番し、旧 entry は orphan として保持＝
            // 旧値は Scene 側で unresolved として残る）。rename を意図する場合は [SerializedFieldID] か
            // .cs.meta の fieldID 手当てで対応する。iteration を妨げないため警告にとどめる。
            Console.WriteLine($"[ScriptMetaSync] {diagnostics.ambiguousCount} ambiguous rename(s) treated as new fields " +
                "(previous values preserved as unresolved). Add [SerializedFieldID] or edit .cs.meta to map a rename.");
        }
        Console.WriteLine($"[ScriptMetaSync] done. files={perFile.Count} changed={anyChange}");
        return 0;
    }

    private static Mode ParseMode(string value) {
        return string.Equals(value, "ValidateOnly", StringComparison.OrdinalIgnoreCase) ? Mode.ValidateOnly : Mode.EditorSync;
    }

    // 1 ファイルの .cs.meta を同期する。変更があれば true

}
