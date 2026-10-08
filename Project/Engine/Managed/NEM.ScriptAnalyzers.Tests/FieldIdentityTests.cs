using System.Text.Json.Nodes;
using NEM.ScriptMetaSync;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace NEM.ScriptAnalyzers.Tests;

internal static class FieldIdentityTests {

    private const string OldID = "14501b26-1c63-4f93-8ba9-87bbaf74ec02";

    internal static void Run() {

        CheckInvalidMetadata();

        var diagnostics = new ScriptMetaDiagnostics();
        var planner = new ScriptMetaPlanner(diagnostics);
        var existing = new JsonObject {
            ["fields"] = new JsonArray {
                new JsonObject { ["name"] = "speed", ["type"] = "float", ["fieldId"] = OldID }
            }
        };
        var model = new ScriptModel { FullTypeName = "Fixtures.Player" };
        model.Fields.Add(new FieldModel { Name = "title", TypeText = "string" });

        // 1件の削除と追加でも名前変更を推測しない
        JsonArray unrelated = planner.BuildFields(model, existing, Mode.EditorSync);
        Check((string?)unrelated[0]?["fieldId"] != OldID && diagnostics.errorCount == 0);
        Check(model.Fields[0].FormerlySerializedAs.Count == 0);
        Check((string?)existing["fields"]?[0]?["name"] == "speed");

        // 明示した旧名は型が変わってもIDを維持する
        model.Fields[0].FormerlySerializedAs.Add("speed");
        JsonArray renamed = planner.BuildFields(model, existing, Mode.ValidateOnly);
        Check((string?)renamed[0]?["fieldId"] == OldID && diagnostics.errorCount == 0);
        Check((string?)renamed[0]?["formerNames"]?[0] == "speed");

        // 同期済みmetaを再検証してもIDと旧名を変えない
        var synchronized = new JsonObject { ["fields"] = renamed.DeepClone() };
        JsonArray repeated = planner.BuildFields(model, synchronized, Mode.ValidateOnly);
        Check(JsonNode.DeepEquals(renamed, repeated));

        // 属性なしの追加は検証だけでは採番しない
        model.Fields[0].FormerlySerializedAs.Clear();
        planner.BuildFields(model, existing, Mode.ValidateOnly);
        Check(diagnostics.errorCount == 1);
        // 属性付き非公開Fieldと保存除外をmetaにも反映する
        var source = CSharpSyntaxTree.ParseText("""
            using Base = NEMEngine.MonoBehaviour;
            using Save = NEMEngine.SerializeFieldAttribute;
            abstract class Parent : Base { [Save] private int inherited; }
            class Fixture : Parent {
                [SerializeReference] private object shared;
                [NonSerialized, SerializeField] public int excluded;
                [SerializeField] private readonly int immutable;
                public int value;
            }
            namespace Unrelated { class MonoBehaviour {} class Decoy : MonoBehaviour { public int value; } }
            """, path: "fixture.cs");
        var collected = ScriptSourceCollector.Collect(new[] { source });
        Check(collected["fixture.cs"].Count == 1);
        Check(collected["fixture.cs"][0].Fields.Select(field => field.Name).SequenceEqual(new[] { "inherited", "shared", "value" }));
        TestConflictingNames(existing);
        Console.WriteLine("[PASS] field rename requires explicit and unambiguous identity.");
    }

    // 複数Fieldが同じ旧名を要求した場合は診断する
    private static void TestConflictingNames(JsonObject existing) {
        var diagnostics = new ScriptMetaDiagnostics();
        var planner = new ScriptMetaPlanner(diagnostics);
        var model = new ScriptModel { FullTypeName = "Fixtures.Player" };
        model.Fields.Add(new FieldModel { Name = "first", FormerlySerializedAs = new() { "speed" } });
        model.Fields.Add(new FieldModel { Name = "second", FormerlySerializedAs = new() { "speed" } });
        planner.BuildFields(model, existing, Mode.EditorSync);
        Check(diagnostics.errorCount > 0);

        // 一つのFieldが異なる旧IDを指す場合も決め打ちしない
        diagnostics = new ScriptMetaDiagnostics();
        planner = new ScriptMetaPlanner(diagnostics);
        var duplicate = existing.DeepClone().AsObject();
        duplicate["fields"]!.AsArray().Add(new JsonObject {
            ["name"] = "oldTitle", ["fieldId"] = "f2023fa9-f7f6-4f68-af38-461fc521a01a",
        });
        model.Fields.RemoveAt(1);
        model.Fields[0].FormerlySerializedAs.Add("oldTitle");
        planner.BuildFields(model, duplicate, Mode.EditorSync);
        Check(diagnostics.errorCount > 0);
    }

    // 旧名情報の破損と別IDへの二重対応を拒否する
    private static void CheckInvalidMetadata() {
        var script = new JsonObject {
            ["fullTypeName"] = "Fixture", ["scriptTypeId"] = OldID,
            ["fields"] = new JsonArray(new JsonObject { ["name"] = "speed", ["fieldId"] = OldID }),
        };
        var meta = new JsonObject { ["scripts"] = new JsonArray(script) };
        ScriptMetaStorage.ValidateMetadata(meta);
        foreach (JsonNode malformed in new JsonNode[] { JsonValue.Create("old")!, new JsonArray(1), new JsonArray("") }) {
            script["formerNames"] = malformed;
            bool failed = false;
            try { ScriptMetaStorage.ValidateMetadata(meta); }
            catch (System.Text.Json.JsonException) { failed = true; }
            Check(failed);
        }
        script.Remove("formerNames");
        script["fields"]!.AsArray().Add(new JsonObject {
            ["name"] = "other", ["fieldId"] = "f2023fa9-f7f6-4f68-af38-461fc521a01a", ["formerNames"] = new JsonArray("speed"),
        });
        bool ambiguous = false;
        try { ScriptMetaStorage.ValidateMetadata(meta); }
        catch (System.Text.Json.JsonException) { ambiguous = true; }
        Check(ambiguous);
    }

    private static void Check(bool result) {

        if (!result) {
            throw new InvalidOperationException("Field identity contract failed.");
        }
    }
}
