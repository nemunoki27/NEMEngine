using System.Reflection;
namespace NEMEngine;

// 登録型と保存フィールドの対応
internal sealed class ScriptTypeEntry {

    // 型と通知先を一組で確定する
    internal ScriptTypeEntry(Type type) {

        this.type = type;
        callbacks = new ScriptCallbacks(type);
    }

    internal string scriptTypeID = string.Empty;   // 正規化済みGUID
    internal readonly Type type;
    internal readonly ScriptCallbacks callbacks;
    internal string fullTypeName = string.Empty;
    internal string displayName = string.Empty;
    internal string sourcePath = string.Empty;
    internal bool hasExplicitID;
    internal int defaultExecutionOrder;   // 既定の実行順序

    // 型登録時に確定した保存schema
    internal string schemaJSON = string.Empty;
    // 保存FieldIDと反射情報の対応
    internal readonly Dictionary<string, FieldInfo> fieldMap = new(StringComparer.Ordinal);
    // 未対応の保存値は変換せず保持する
    internal readonly HashSet<string> unsupportedFields = new(StringComparer.Ordinal);
    // Inspectorで表示できるフィールドだけを取得する
    internal readonly Dictionary<string, FieldInfo> runtimeFieldMap = new(StringComparer.Ordinal);
    // 個体生成後に参照を解決するFieldID
    internal readonly HashSet<string> deferredFields = new(StringComparer.Ordinal);
}
