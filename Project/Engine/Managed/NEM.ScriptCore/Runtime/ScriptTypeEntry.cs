using System.Reflection;









namespace NEMEngine;

// 登録型と保存フィールドの対応
internal sealed class ScriptTypeEntry {

    internal string scriptTypeID = string.Empty;   // 正規化済み GUID
    internal Type type = null!;
    internal string fullTypeName = string.Empty;
    internal string displayName = string.Empty;
    internal string sourcePath = string.Empty;
    internal bool hasExplicitID;
    internal int defaultExecutionOrder;   // [DefaultExecutionOrder] の値（未指定は 0）

    // serialized field schema（defaultValueJson を含む完成形 JSON）。C++ へ blob で渡す。
    // 構築は load 時の一度きり。
    internal string schemaJson = string.Empty;
    // Stable Field GUID -> FieldInfo。runtime get/set と authoring 適用に使う（hot path では reflection しない）
    internal Dictionary<string, FieldInfo> fieldMap = new(StringComparer.Ordinal);
    // Inspectorで表示できるフィールドだけを取得する
    internal Dictionary<string, FieldInfo> runtimeFieldMap = new(StringComparer.Ordinal);
    // 参照解決を全インスタンス生成後まで遅らせるフィールドのGUID集合(Entity/Component/ScriptBehaviour参照)
    internal HashSet<string> deferredFields = new(StringComparer.Ordinal);
}
