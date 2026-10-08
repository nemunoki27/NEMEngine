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
        string? project = null;
        string configuration = "Develop";
        Mode mode = Mode.EditorSync;

        for (int i = 0; i < args.Length; ++i) {
            switch (args[i]) {
                case "--root": if (i + 1 < args.Length) { root = args[++i]; } break;
                case "--project": if (i + 1 < args.Length) { project = args[++i]; } break;
                case "--configuration": if (i + 1 < args.Length) { configuration = args[++i]; } break;
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

        Dictionary<string, List<ScriptModel>> perFile;
        try {
            // 標準配置では実Projectを解決し、独自の条件定義も反映する
            string conventional = Path.GetFullPath(Path.Combine(root, "..", "Scripts", "GameScripts.csproj"));
            project ??= File.Exists(conventional) ? conventional : null;
            perFile = project == null ? ScriptSourceCollector.Collect(root) :
                ScriptSourceCollector.Collect(ScriptCompilationInput.Read(project, configuration));
        }
        catch (Exception ex) {
            Console.Error.WriteLine($"[ScriptMetaSync] source analysis failed: {ex.Message}");
            return 1;
        }

        // GUID の正規化・重複検出（scriptTypeID は全体一意、fieldID は型内一意）
        var seenScriptIDs = new Dictionary<string, string>(StringComparer.Ordinal);

        bool anyChange = false;
        var plans = new List<(string path, JsonObject meta)>();
        try {
            foreach ((string file, List<ScriptModel> models) in perFile) {
                JsonObject meta = storage.LoadMeta(file + ".meta");
                JsonObject planned = planner.Plan(file, meta, models, mode, seenScriptIDs);
                ScriptMetaStorage.ValidateMetadata(planned);
                plans.Add((file + ".meta", planned));
            }
            // renameと孤立entryの保持が完了した後のIDも全体で検査する
            var finalIDs = new HashSet<Guid>();
            foreach (var plan in plans) {
                foreach (JsonNode? script in plan.meta["scripts"]!.AsArray()) {
                    if (!finalIDs.Add(Guid.Parse((string)script!["scriptTypeId"]!))) {
                        throw new JsonException("Duplicate final script ID: " + script["scriptTypeId"]);
                    }
                }
            }
        } catch (Exception ex) {
            Console.Error.WriteLine($"[ScriptMetaSync] metadata planning failed: {ex.Message}");
            return 1;
        }

        // 全候補が有効な場合だけmetaを書き換える
        if (diagnostics.errorCount == 0) {
            foreach ((string path, JsonObject meta) in plans) {
                anyChange |= storage.WriteMetaIfChanged(path, meta, mode);
            }
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
