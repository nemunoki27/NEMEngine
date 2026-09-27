using System.Reflection;
using System.Text.Json;

namespace NEMEngine;

// 生成順で未解決となったFieldの適用を保留する
internal sealed class PendingScriptReferences {

    private readonly LinkedList<(MonoBehaviour script, FieldInfo field, JsonElement value)> fields = new();
    private readonly Action<MonoBehaviour, FieldInfo, JsonElement> apply;
    private readonly List<(MonoBehaviour script, FieldInfo field, JsonElement value, object? expected)> unresolved = new();
    private bool flushing;

    internal PendingScriptReferences(Action<MonoBehaviour, FieldInfo, JsonElement> apply) {
        this.apply = apply;
    }

    // JSON文書の破棄後も値を保持する
    internal void Add(MonoBehaviour script, FieldInfo field, JsonElement value) {
        fields.AddLast((script, field, value.Clone()));
    }

    // 実行値を保持し、SceneやScriptの生成後に再解決する
    internal void AddUnresolved(MonoBehaviour script, FieldInfo field, JsonElement value) {
        unresolved.RemoveAll(item => ReferenceEquals(item.script, script) && item.field.Equals(field));
        unresolved.Add((script, field, value.Clone(), field.GetValue(script)));
    }

    internal void RetryUnresolved() {
        foreach (var item in unresolved) {
            // ゲームコードが設定した値を遅延解決で上書きしない
            if (MatchesExpected(item.script, item.field, item.expected)) {
                Add(item.script, item.field, item.value);
            }
        }
        unresolved.Clear();
    }

    internal void DiscardChangedReferences(Action<MonoBehaviour, FieldInfo> changed) {
        for (int index = unresolved.Count - 1; index >= 0; --index) {
            var item = unresolved[index];
            if (!MatchesExpected(item.script, item.field, item.expected)) {
                unresolved.RemoveAt(index);
                changed(item.script, item.field);
            }
        }
    }

    private static bool MatchesExpected(MonoBehaviour script, FieldInfo field, object? expected) {
        object? current = field.GetValue(script);
        return field.FieldType.IsValueType ? Equals(current, expected) : ReferenceEquals(current, expected);
    }

    internal void Remove(MonoBehaviour script, FieldInfo field) {
        unresolved.RemoveAll(item => ReferenceEquals(item.script, script) && item.field.Equals(field));
        for (var node = fields.First; node != null;) {
            var next = node.Next;
            if (ReferenceEquals(node.Value.script, script) && node.Value.field.Equals(field)) { fields.Remove(node); }
            node = next;
        }
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

    internal void Clear() { fields.Clear(); unresolved.Clear(); }

    internal void Remove(MonoBehaviour script) {
        unresolved.RemoveAll(item => ReferenceEquals(item.script, script));
        for (var node = fields.First; node != null;) {
            var next = node.Next;
            if (ReferenceEquals(node.Value.script, script)) { fields.Remove(node); }
            node = next;
        }
    }
}
