using System.Collections;

namespace NEMEngine;

// 保存元から順に保存対象のcallbackを呼ぶ
internal static class ScriptSerializationCallbacks {

    // 所有Scriptから保存対象の通知を開始
    internal static void Invoke(MonoBehaviour host, bool beforeSerialize, bool includePrivate = false) {

        Visit(host, beforeSerialize, new HashSet<object>(ReferenceEqualityComparer.Instance), true,
            includePrivate: includePrivate);
    }

    // 訪問済みの個体を除いて保存Fieldを辿る
    private static void Visit(object? value, bool beforeSerialize, HashSet<object> visited, bool host = false, int depth = 0,
        bool includePrivate = false) {
        if (value is null || !visited.Add(value)) {
            return;
        }
        if (depth > 64) {
            throw new InvalidOperationException("Serialization callback nesting exceeds 64 levels.");
        }
        Type type = value.GetType();
        if (!host && typeof(Object).IsAssignableFrom(type)) {
            return;
        }
        if (value is IList items) {
            for (int i = 0; i < items.Count; ++i) {
                object? item = items[i];
                Visit(item, beforeSerialize, visited, depth: depth + 1, includePrivate: includePrivate);
                if (item is not null && item.GetType().IsValueType) {
                    items[i] = item;
                }
            }
            return;
        }
        if (!host && !ScriptFieldTypeUtility.IsSerializableObjectType(type)) {
            return;
        }

        // 保存元の加工後に参照先を辿る
        if (value is ISerializationCallbackReceiver receiver) {
            if (beforeSerialize) {
                receiver.OnBeforeSerialize();
            } else {
                receiver.OnAfterDeserialize();
            }
        }
        foreach (var field in ScriptFieldTypeUtility.EnumerateNestedSerializedFields(
            type, includePrivate, host ? typeof(MonoBehaviour) : null)) {
            object? member = field.GetValue(value);
            Visit(member, beforeSerialize, visited, depth: depth + 1, includePrivate: includePrivate);
            if (field.FieldType.IsValueType) {
                field.SetValue(value, member);
            }
        }
    }
}
