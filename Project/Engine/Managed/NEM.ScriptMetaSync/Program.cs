using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace NEM.ScriptMetaSync;

// script metadata sidecar(.cs.meta)を Stable ID の正として維持するツール。
// .cs を Roslyn(syntax) で解析し、concrete ScriptBehaviour 型と serialized field を検出して
// 各 .cs.meta の "scripts" ブロックへ UUID v4 を採番・維持する。
// EditorSync: 不足を自動採番して安全に書き込む / ValidateOnly: 変更せず不足を error 終了。
internal static class Program {

    private const string ScriptBehaviourName = "ScriptBehaviour";

    private enum Mode { EditorSync, ValidateOnly }

    // 検出した 1 フィールド
    private sealed class FieldModel {
        public string Name = string.Empty;
        public string TypeText = string.Empty;
        public string? ExplicitID;            // [SerializedFieldID]
        public List<string> FormerlySerializedAs = new();
    }

    // 検出した 1 script 型
    private sealed class ScriptModel {
        public string FullTypeName = string.Empty;
        public string? ExplicitID;            // [ScriptTypeID]
        public List<string> FormerlyKnown = new();   // [FormerlyKnownScriptType]
        public List<FieldModel> Fields = new();
    }

    private sealed class ClassInfo {
        public string SimpleName = string.Empty;
        public string? BaseSimpleName;
        public bool IsAbstract;
    }

    private static int errorCount = 0;
    private static int ambiguousCount = 0;

    private static int Main(string[] args) {

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

        // .cs を収集（生成物・VCS・reload 作業領域は除外）
        var csFiles = new List<string>();
        foreach (string file in Directory.EnumerateFiles(root, "*.cs", SearchOption.AllDirectories)) {
            if (IsExcluded(file)) { continue; }
            csFiles.Add(file);
        }

        // 全 class の継承マップ（transitive に ScriptBehaviour 派生を判定するため）
        var classMap = new Dictionary<string, ClassInfo>(StringComparer.Ordinal);
        var perFile = new Dictionary<string, List<ScriptModel>>(StringComparer.OrdinalIgnoreCase);
        var parsedRoots = new Dictionary<string, CompilationUnitSyntax>(StringComparer.OrdinalIgnoreCase);

        foreach (string file in csFiles) {
            string text = File.ReadAllText(file);
            SyntaxTree tree = CSharpSyntaxTree.ParseText(text);
            var unit = (CompilationUnitSyntax)tree.GetRoot();
            parsedRoots[file] = unit;
            foreach (ClassDeclarationSyntax cls in unit.DescendantNodes().OfType<ClassDeclarationSyntax>()) {
                ClassInfo info = new() {
                    SimpleName = cls.Identifier.Text,
                    BaseSimpleName = FirstBaseSimpleName(cls),
                    IsAbstract = cls.Modifiers.Any(m => m.IsKind(SyntaxKind.AbstractKeyword)),
                };
                classMap[info.SimpleName] = info;
            }
        }

        // 各ファイルから script 型を抽出する
        foreach (string file in csFiles) {
            var models = ExtractScripts(parsedRoots[file], classMap);
            if (models.Count > 0) {
                perFile[file] = models;
            }
        }

        // GUID の正規化・重複検出（scriptTypeID は全体一意、fieldID は型内一意）
        var seenScriptIDs = new Dictionary<string, string>(StringComparer.Ordinal);

        bool anyChange = false;
        foreach ((string file, List<ScriptModel> models) in perFile) {
            anyChange |= SyncFile(file, models, mode, seenScriptIDs);
        }

        if (errorCount > 0) {
            Console.Error.WriteLine($"[ScriptMetaSync] failed with {errorCount} error(s).");
            return 1;
        }
        if (ambiguousCount > 0) {
            // 曖昧 rename は build/reload を止めない（非破壊で新規 UUID を採番し、旧 entry は orphan として保持＝
            // 旧値は Scene 側で unresolved として残る）。rename を意図する場合は [SerializedFieldID] か
            // .cs.meta の fieldID 手当てで対応する。iteration を妨げないため警告にとどめる。
            Console.WriteLine($"[ScriptMetaSync] {ambiguousCount} ambiguous rename(s) treated as new fields " +
                "(previous values preserved as unresolved). Add [SerializedFieldID] or edit .cs.meta to map a rename.");
        }
        Console.WriteLine($"[ScriptMetaSync] done. files={perFile.Count} changed={anyChange}");
        return 0;
    }

    private static Mode ParseMode(string value) {
        return string.Equals(value, "ValidateOnly", StringComparison.OrdinalIgnoreCase) ? Mode.ValidateOnly : Mode.EditorSync;
    }

    private static bool IsExcluded(string path) {
        string norm = path.Replace('\\', '/');
        string[] excluded = { "/bin/", "/obj/", "/Generated/", "/Staging/", "/Shadow/", "/LastKnownGood/", "/.git/", "/.vs/" };
        foreach (string e in excluded) {
            if (norm.Contains(e, StringComparison.OrdinalIgnoreCase)) { return true; }
        }
        return norm.EndsWith(".g.cs", StringComparison.OrdinalIgnoreCase)
            || norm.EndsWith(".generated.cs", StringComparison.OrdinalIgnoreCase);
    }

    private static string? FirstBaseSimpleName(ClassDeclarationSyntax cls) {
        if (cls.BaseList == null) { return null; }
        foreach (BaseTypeSyntax baseType in cls.BaseList.Types) {
            // 最初の base を基底クラス候補とする（interface でも simple 名で照合する）
            return SimpleNameOf(baseType.Type);
        }
        return null;
    }

    private static string SimpleNameOf(TypeSyntax type) {
        return type switch {
            IdentifierNameSyntax id => id.Identifier.Text,
            QualifiedNameSyntax q => q.Right.Identifier.Text,
            GenericNameSyntax g => g.Identifier.Text,
            _ => type.ToString(),
        };
    }

    private static bool DerivesFromScriptBehaviour(string? simpleName, Dictionary<string, ClassInfo> map) {
        int guard = 0;
        string? current = simpleName;
        while (!string.IsNullOrEmpty(current) && guard++ < 64) {
            if (current == ScriptBehaviourName) { return true; }
            if (!map.TryGetValue(current!, out ClassInfo? info)) { return false; }
            current = info.BaseSimpleName;
        }
        return false;
    }

    private static List<ScriptModel> ExtractScripts(CompilationUnitSyntax unit, Dictionary<string, ClassInfo> map) {

        var result = new List<ScriptModel>();
        foreach (ClassDeclarationSyntax cls in unit.DescendantNodes().OfType<ClassDeclarationSyntax>()) {

            if (cls.Modifiers.Any(m => m.IsKind(SyntaxKind.AbstractKeyword))) { continue; }
            if (!DerivesFromScriptBehaviour(FirstBaseSimpleName(cls), map)) { continue; }

            string ns = NamespaceOf(cls);
            var model = new ScriptModel {
                FullTypeName = string.IsNullOrEmpty(ns) ? cls.Identifier.Text : ns + "." + cls.Identifier.Text,
            };
            model.ExplicitID = AttributeStringArg(cls.AttributeLists, "ScriptTypeID");
            model.FormerlyKnown = AttributeStringArgs(cls.AttributeLists, "FormerlyKnownScriptType");

            foreach (FieldDeclarationSyntax field in cls.Members.OfType<FieldDeclarationSyntax>()) {

                if (!IsSerializedField(field)) { continue; }
                string typeText = field.Declaration.Type.ToString();
                string? explicitID = AttributeStringArg(field.AttributeLists, "SerializedFieldID");
                List<string> formerly = AttributeStringArgs(field.AttributeLists, "FormerlySerializedAs");

                foreach (VariableDeclaratorSyntax v in field.Declaration.Variables) {
                    model.Fields.Add(new FieldModel {
                        Name = v.Identifier.Text,
                        TypeText = typeText,
                        ExplicitID = explicitID,
                        FormerlySerializedAs = new List<string>(formerly),
                    });
                }
            }
            result.Add(model);
        }
        return result;
    }

    private static string NamespaceOf(SyntaxNode node) {
        for (SyntaxNode? n = node.Parent; n != null; n = n.Parent) {
            if (n is FileScopedNamespaceDeclarationSyntax fs) { return fs.Name.ToString(); }
            if (n is NamespaceDeclarationSyntax ns) { return ns.Name.ToString(); }
        }
        return string.Empty;
    }

    private static bool IsSerializedField(FieldDeclarationSyntax field) {
        bool isStatic = field.Modifiers.Any(m => m.IsKind(SyntaxKind.StaticKeyword));
        bool isConst = field.Modifiers.Any(m => m.IsKind(SyntaxKind.ConstKeyword));
        bool isReadonly = field.Modifiers.Any(m => m.IsKind(SyntaxKind.ReadOnlyKeyword));
        if (isStatic || isConst || isReadonly) { return false; }
        bool isPublic = field.Modifiers.Any(m => m.IsKind(SyntaxKind.PublicKeyword));
        bool hasSerializeField = HasAttribute(field.AttributeLists, "SerializeField");
        return isPublic || hasSerializeField;
    }

    private static bool HasAttribute(SyntaxList<AttributeListSyntax> lists, string name) {
        foreach (AttributeListSyntax list in lists) {
            foreach (AttributeSyntax attr in list.Attributes) {
                if (MatchesAttributeName(attr, name)) { return true; }
            }
        }
        return false;
    }

    private static string? AttributeStringArg(SyntaxList<AttributeListSyntax> lists, string name) {
        foreach (AttributeListSyntax list in lists) {
            foreach (AttributeSyntax attr in list.Attributes) {
                if (MatchesAttributeName(attr, name) && attr.ArgumentList?.Arguments.Count >= 1) {
                    return LiteralString(attr.ArgumentList.Arguments[0].Expression);
                }
            }
        }
        return null;
    }

    private static List<string> AttributeStringArgs(SyntaxList<AttributeListSyntax> lists, string name) {
        var values = new List<string>();
        foreach (AttributeListSyntax list in lists) {
            foreach (AttributeSyntax attr in list.Attributes) {
                if (MatchesAttributeName(attr, name) && attr.ArgumentList?.Arguments.Count >= 1) {
                    string? s = LiteralString(attr.ArgumentList.Arguments[0].Expression);
                    if (!string.IsNullOrWhiteSpace(s)) { values.Add(s!); }
                }
            }
        }
        return values;
    }

    private static bool MatchesAttributeName(AttributeSyntax attr, string name) {
        string text = SimpleNameOf(attr.Name);
        return text == name || text == name + "Attribute";
    }

    private static string? LiteralString(ExpressionSyntax expr) {
        return expr is LiteralExpressionSyntax lit && lit.IsKind(SyntaxKind.StringLiteralExpression)
            ? lit.Token.ValueText : null;
    }

    // 1 ファイルの .cs.meta を同期する。変更があれば true
    private static bool SyncFile(string csPath, List<ScriptModel> models, Mode mode,
        Dictionary<string, string> seenScriptIDs) {

        string metaPath = csPath + ".meta";
        JsonObject meta = LoadMeta(metaPath);

        var existingScripts = meta["scripts"] as JsonArray ?? new JsonArray();
        var newScripts = new JsonArray();
        var matchedExisting = new HashSet<JsonObject>();

        // 1. fullTypeName 一致 / 単純 rename で既存 script entry を対応付ける
        foreach (ScriptModel model in models) {

            JsonObject? existing = FindScriptByName(existingScripts, model.FullTypeName, matchedExisting);
            string? renamedFrom = null;
            if (existing == null) {
                // [FormerlyKnownScriptType] で旧名照合
                foreach (string former in model.FormerlyKnown) {
                    existing = FindScriptByName(existingScripts, former, matchedExisting);
                    if (existing != null) { renamedFrom = former; break; }
                }
            }

            if (existing != null) { matchedExisting.Add(existing); }
            newScripts.Add(BuildScriptEntry(model, existing, renamedFrom, mode, seenScriptIDs));
        }

        // 2. source から消えた script type の単純 rename 推定（1 消失 + 1 新規・既存と未対応）
        ResolveScriptRenames(csPath, existingScripts, matchedExisting, models, newScripts, mode);

        // 3. 未対応の既存 script は破壊せず保持する（値の round-trip のため）
        foreach (JsonNode? node in existingScripts) {
            if (node is JsonObject obj && !matchedExisting.Contains(obj)) {
                Console.WriteLine($"[ScriptMetaSync] keep orphan script entry '{obj["fullTypeName"]}' in {Path.GetFileName(metaPath)} (source not found).");
                newScripts.Add(obj.DeepClone());
            }
        }

        // scriptTypeID で安定ソート
        var sorted = newScripts.OfType<JsonObject>().OrderBy(s => (string?)s["scriptTypeId"] ?? "", StringComparer.Ordinal).ToList();
        var sortedArray = new JsonArray();
        foreach (JsonObject s in sorted) { sortedArray.Add(s.DeepClone()); }

        meta["scripts"] = sortedArray;

        return WriteMetaIfChanged(metaPath, meta, mode);
    }

    private static JsonObject? FindScriptByName(JsonArray scripts, string fullTypeName, HashSet<JsonObject> used) {
        foreach (JsonNode? node in scripts) {
            if (node is JsonObject obj && !used.Contains(obj) &&
                (string?)obj["fullTypeName"] == fullTypeName) {
                return obj;
            }
        }
        return null;
    }

    private static void ResolveScriptRenames(string csPath, JsonArray existingScripts,
        HashSet<JsonObject> matchedExisting, List<ScriptModel> models, JsonArray newScripts, Mode mode) {

        // newScripts のうち、scriptTypeID が新規採番された(=既存対応が無かった)もの
        var unresolvedNew = newScripts.OfType<JsonObject>()
            .Where(s => s["__isNew"]?.GetValue<bool>() == true).ToList();
        var unmatchedExisting = existingScripts.OfType<JsonObject>()
            .Where(s => !matchedExisting.Contains(s)).ToList();

        if (unresolvedNew.Count == 1 && unmatchedExisting.Count == 1) {

            // 安全な単純 rename：旧 scriptTypeID を維持し formerNames へ旧名を追加
            JsonObject newEntry = unresolvedNew[0];
            JsonObject oldEntry = unmatchedExisting[0];
            string oldName = (string?)oldEntry["fullTypeName"] ?? "";
            string oldID = (string?)oldEntry["scriptTypeId"] ?? "";
            if (!string.IsNullOrEmpty(oldID)) {
                newEntry["scriptTypeId"] = oldID;
                AddFormerName(newEntry, oldName);
                MergeFieldsFromRenamedScript(newEntry, oldEntry);
                matchedExisting.Add(oldEntry);
                Console.WriteLine($"[ScriptMetaSync] script rename: '{oldName}' -> '{newEntry["fullTypeName"]}' (id kept).");
            }
        } else if (unresolvedNew.Count > 0 && unmatchedExisting.Count > 0) {

            // 曖昧：自動決定しない。診断を出して保留する
            ambiguousCount++;
            Console.Error.WriteLine($"[ScriptMetaSync] ambiguous script rename in {Path.GetFileName(csPath)}: " +
                $"new=[{string.Join(",", unresolvedNew.Select(s => (string?)s["fullTypeName"]))}] " +
                $"removed=[{string.Join(",", unmatchedExisting.Select(s => (string?)s["fullTypeName"]))}]. Resolve manually.");
        }

        // 内部マーカーを除去
        foreach (JsonObject s in newScripts.OfType<JsonObject>().ToList()) {
            s.Remove("__isNew");
        }
    }

    private static void MergeFieldsFromRenamedScript(JsonObject newEntry, JsonObject oldEntry) {
        // rename された型の field id を、名前一致で引き継ぐ
        if (oldEntry["fields"] is not JsonArray oldFields || newEntry["fields"] is not JsonArray newFields) {
            return;
        }
        foreach (JsonObject nf in newFields.OfType<JsonObject>()) {
            string name = (string?)nf["name"] ?? "";
            foreach (JsonObject of in oldFields.OfType<JsonObject>()) {
                if ((string?)of["name"] == name && of["fieldId"] != null) {
                    nf["fieldId"] = (string?)of["fieldId"];
                    break;
                }
            }
        }
    }

    private static JsonObject BuildScriptEntry(ScriptModel model, JsonObject? existing,
        string? renamedFrom, Mode mode, Dictionary<string, string> seenScriptIDs) {

        var entry = new JsonObject();
        entry["fullTypeName"] = model.FullTypeName;

        // scriptTypeID: explicit attribute → existing meta → 新規 UUID
        string? scriptID = NormalizeOrError(model.ExplicitID, $"{model.FullTypeName} [ScriptTypeID]");
        bool isNew = false;
        if (scriptID == null) {
            scriptID = (string?)existing?["scriptTypeId"];
            scriptID = NormalizeOrError(scriptID, $"{model.FullTypeName} meta scriptTypeId");
        }
        if (scriptID == null) {
            if (mode == Mode.ValidateOnly) {
                errorCount++;
                Console.Error.WriteLine($"[ScriptMetaSync] missing scriptTypeId for '{model.FullTypeName}'. Run Editor metadata sync.");
                scriptID = "00000000-0000-0000-0000-000000000000";
            } else {
                scriptID = Guid.NewGuid().ToString("D");
                isNew = true;
            }
        }

        // 全体での scriptTypeID 重複検出
        if (seenScriptIDs.TryGetValue(scriptID, out string? owner) && owner != model.FullTypeName) {
            errorCount++;
            Console.Error.WriteLine($"[ScriptMetaSync] duplicate scriptTypeId '{scriptID}' on '{model.FullTypeName}' and '{owner}'.");
        } else {
            seenScriptIDs[scriptID] = model.FullTypeName;
        }

        entry["scriptTypeId"] = scriptID;
        if (isNew) { entry["__isNew"] = true; }

        // formerNames（既存 + [FormerlyKnownScriptType] + rename 由来）
        var formerNames = new JsonArray();
        var formerSet = new HashSet<string>(StringComparer.Ordinal);
        void AddFormer(string? n) { if (!string.IsNullOrWhiteSpace(n) && formerSet.Add(n!)) { formerNames.Add(n!); } }
        if (existing?["formerNames"] is JsonArray ef) { foreach (JsonNode? n in ef) { AddFormer((string?)n); } }
        foreach (string fk in model.FormerlyKnown) { AddFormer(fk); }
        if (renamedFrom != null) { AddFormer(renamedFrom); }
        entry["formerNames"] = formerNames;

        // fields
        entry["fields"] = BuildFields(model, existing, mode);
        return entry;
    }

    private static JsonArray BuildFields(ScriptModel model, JsonObject? existingScript, Mode mode) {

        var existingFields = existingScript?["fields"] as JsonArray ?? new JsonArray();
        var usedExisting = new HashSet<JsonObject>();
        var seenFieldIDs = new Dictionary<string, string>(StringComparer.Ordinal);

        // 1) 名前 / formerNames / 明示 ID で対応付け
        var resolved = new List<(FieldModel model, JsonObject? existing, string? id)>();
        var needAssign = new List<int>();

        foreach (FieldModel f in model.Fields) {
            JsonObject? match = FindFieldByName(existingFields, f.Name, usedExisting);
            if (match == null) {
                foreach (string former in f.FormerlySerializedAs) {
                    match = FindFieldByName(existingFields, former, usedExisting);
                    if (match != null) { break; }
                }
            }
            if (match != null) { usedExisting.Add(match); }

            string? id = NormalizeOrError(f.ExplicitID, $"{model.FullTypeName}.{f.Name} [SerializedFieldID]");
            if (id == null) { id = NormalizeOrError((string?)match?["fieldId"], $"{model.FullTypeName}.{f.Name} meta fieldId"); }
            resolved.Add((f, match, id));
            if (id == null) { needAssign.Add(resolved.Count - 1); }
        }

        // 2) 未割当 field と、未対応の既存 field（削除）で単純 rename 推定
        var unmatchedExisting = existingFields.OfType<JsonObject>().Where(e => !usedExisting.Contains(e)).ToList();
        if (needAssign.Count == 1 && unmatchedExisting.Count == 1) {

            int idx = needAssign[0];
            JsonObject old = unmatchedExisting[0];
            string oldName = (string?)old["name"] ?? "";
            string? oldID = NormalizeOrError((string?)old["fieldId"], "rename fieldId");
            if (oldID != null) {
                resolved[idx] = (resolved[idx].model, old, oldID);
                resolved[idx].model.FormerlySerializedAs.Add(oldName);
                usedExisting.Add(old);
                needAssign.Clear();
                Console.WriteLine($"[ScriptMetaSync] field rename: {model.FullTypeName}.{oldName} -> {resolved[idx].model.Name} (id kept).");
            }
        } else if (needAssign.Count > 0 && unmatchedExisting.Count > 0) {
            ambiguousCount++;
            Console.Error.WriteLine($"[ScriptMetaSync] ambiguous field rename in {model.FullTypeName}: " +
                $"new=[{string.Join(",", needAssign.Select(i => resolved[i].model.Name))}] " +
                $"removed=[{string.Join(",", unmatchedExisting.Select(e => (string?)e["name"]))}]. Resolve manually.");
        }

        // 3) 残りの未割当は新規 UUID（EditorSync）/ error（ValidateOnly）
        var fields = new JsonArray();
        foreach ((FieldModel f, JsonObject? existing, string? idIn) in resolved) {
            string? id = idIn;
            if (id == null) {
                if (mode == Mode.ValidateOnly) {
                    errorCount++;
                    Console.Error.WriteLine($"[ScriptMetaSync] missing fieldId for '{model.FullTypeName}.{f.Name}'. Run Editor metadata sync.");
                    id = "00000000-0000-0000-0000-000000000000";
                } else {
                    id = Guid.NewGuid().ToString("D");
                }
            }
            if (seenFieldIDs.TryGetValue(id, out string? owner) && owner != f.Name) {
                errorCount++;
                Console.Error.WriteLine($"[ScriptMetaSync] duplicate fieldId '{id}' on '{model.FullTypeName}.{f.Name}' and '.{owner}'.");
            } else {
                seenFieldIDs[id] = f.Name;
            }

            var fieldNode = new JsonObject {
                ["fieldId"] = id,
                ["name"] = f.Name,
                ["type"] = f.TypeText,
            };
            var former = new JsonArray();
            var formerSet = new HashSet<string>(StringComparer.Ordinal);
            void AddFormer(string? n) { if (!string.IsNullOrWhiteSpace(n) && n != f.Name && formerSet.Add(n!)) { former.Add(n!); } }
            if (existing?["formerNames"] is JsonArray ef) { foreach (JsonNode? n in ef) { AddFormer((string?)n); } }
            foreach (string s in f.FormerlySerializedAs) { AddFormer(s); }
            fieldNode["formerNames"] = former;
            fields.Add(fieldNode);
        }

        // 4) source から消えた field（rename にも該当しない）は meta から外す。
        // meta は「現在の source field の ID 台帳」。orphan を残すと rename 検出を汚染し、
        // 正当な単純 rename まで曖昧扱いになって reload が止まる原因になる。
        // 旧 field の Scene 保存値は scene 側 serializedFields に unresolvedFields として GUID 単位で残るため、
        // meta からの除去は非破壊（識別子は Scene 内に保持される）。

        // fieldID で安定ソート
        var sorted = fields.OfType<JsonObject>().OrderBy(f => (string?)f["fieldId"] ?? "", StringComparer.Ordinal).ToList();
        var sortedArray = new JsonArray();
        foreach (JsonObject f in sorted) { sortedArray.Add(f.DeepClone()); }
        return sortedArray;
    }

    private static JsonObject? FindFieldByName(JsonArray fields, string name, HashSet<JsonObject> used) {
        foreach (JsonNode? node in fields) {
            if (node is JsonObject obj && !used.Contains(obj) && (string?)obj["name"] == name) {
                return obj;
            }
        }
        return null;
    }

    private static void AddFormerName(JsonObject entry, string name) {
        if (string.IsNullOrWhiteSpace(name)) { return; }
        var arr = entry["formerNames"] as JsonArray ?? new JsonArray();
        if (!arr.OfType<JsonNode>().Any(n => (string?)n == name)) {
            arr.Add(name);
        }
        entry["formerNames"] = arr;
    }

    private static string? NormalizeOrError(string? raw, string context) {
        if (string.IsNullOrWhiteSpace(raw)) { return null; }
        if (Guid.TryParse(raw, out Guid guid)) { return guid.ToString("D"); }
        errorCount++;
        Console.Error.WriteLine($"[ScriptMetaSync] invalid GUID '{raw}' ({context}).");
        return null;
    }

    private static JsonObject LoadMeta(string metaPath) {
        try {
            if (File.Exists(metaPath)) {
                JsonNode? node = JsonNode.Parse(File.ReadAllText(metaPath));
                if (node is JsonObject obj) { return obj; }
            }
        }
        catch (Exception ex) {
            Console.Error.WriteLine($"[ScriptMetaSync] failed to read {metaPath}: {ex.Message}");
        }
        // .cs.meta が無ければ最小metaを作る
        return new JsonObject {
            ["schemaVersion"] = 2,
            ["guid"] = NewAssetGuid32(),
            ["type"] = "Script",
            ["importer"] = "ScriptImporter",
            ["importerVersion"] = 1,
            ["settings"] = new JsonObject(),
        };
    }

    private static string NewAssetGuid32() => Guid.NewGuid().ToString("N");

    private static bool WriteMetaIfChanged(string metaPath, JsonObject meta, Mode mode) {

        var options = new JsonSerializerOptions { WriteIndented = true };
        string serialized = meta.ToJsonString(options);

        string? current = File.Exists(metaPath) ? File.ReadAllText(metaPath) : null;
        if (current != null && NormalizeJson(current) == NormalizeJson(serialized)) {
            return false; // 変更なし（reload storm 防止）
        }

        if (mode == Mode.ValidateOnly) {
            errorCount++;
            Console.Error.WriteLine($"[ScriptMetaSync] {Path.GetFileName(metaPath)} is out of sync. Run Editor metadata sync.");
            return false;
        }

        // 一時ファイルへ書いてから置換（壊れた .meta を残さない）
        string tmp = metaPath + ".tmp";
        File.WriteAllText(tmp, serialized, new UTF8Encoding(false));
        File.Move(tmp, metaPath, overwrite: true);
        Console.WriteLine($"[ScriptMetaSync] updated {Path.GetFileName(metaPath)}");
        return true;
    }

    private static string NormalizeJson(string json) {
        try { return JsonNode.Parse(json)?.ToJsonString() ?? json; }
        catch { return json; }
    }
}
