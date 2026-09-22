using System.Reflection;
using System.Text.Json;

namespace NEMEngine;

// 生成順で未解決となったFieldの適用を保留する
internal sealed class PendingScriptReferences {

    private readonly List<(ScriptBehaviour script, FieldInfo field, JsonElement value)> fields = new();
    private readonly Action<ScriptBehaviour, FieldInfo, JsonElement> apply;

    internal PendingScriptReferences(Action<ScriptBehaviour, FieldInfo, JsonElement> apply) {
        this.apply = apply;
    }

    // JSON文書の破棄後も値を保持する
    internal void Add(ScriptBehaviour script, FieldInfo field, JsonElement value) {
        fields.Add((script, field, value.Clone()));
    }

    // 保留した順に適用する
    internal void Flush() {
        if (fields.Count == 0) {
            return;
        }
        foreach ((ScriptBehaviour script, FieldInfo field, JsonElement value) in fields) {
            apply(script, field, value);
        }
        fields.Clear();
    }

    internal void Clear() { fields.Clear(); }
}
