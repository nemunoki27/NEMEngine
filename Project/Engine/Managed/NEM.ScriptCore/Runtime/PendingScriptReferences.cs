using System.Reflection;
using System.Text.Json;

namespace NEMEngine;

// 生成順で未解決となったFieldの適用を保留する
internal sealed class PendingScriptReferences {

    private readonly LinkedList<(MonoBehaviour script, FieldInfo field, JsonElement value)> fields = new();
    private readonly Action<MonoBehaviour, FieldInfo, JsonElement> apply;
    private bool flushing;

    internal PendingScriptReferences(Action<MonoBehaviour, FieldInfo, JsonElement> apply) {
        this.apply = apply;
    }

    // JSON文書の破棄後も値を保持する
    internal void Add(MonoBehaviour script, FieldInfo field, JsonElement value) {
        fields.AddLast((script, field, value.Clone()));
    }

    // 保留した順に適用する
    internal void Flush() {
        if (fields.Count == 0 || flushing) {
            return;
        }
        // 再入中の取消を尊重し、新しい予約は次回へ残す
        var batch = new List<LinkedListNode<(MonoBehaviour script, FieldInfo field, JsonElement value)>>(fields.Count);
        for (var node = fields.First; node != null; node = node.Next) { batch.Add(node); }
        flushing = true;
        try {
            foreach (var node in batch) {
                if (node.List != fields) { continue; }
                fields.Remove(node);
                apply(node.Value.script, node.Value.field, node.Value.value);
            }
        }
        finally { flushing = false; }
    }

    internal void Clear() { fields.Clear(); }

    internal void Remove(MonoBehaviour script) {
        for (var node = fields.First; node != null;) {
            var next = node.Next;
            if (ReferenceEquals(node.Value.script, script)) { fields.Remove(node); }
            node = next;
        }
    }
}
