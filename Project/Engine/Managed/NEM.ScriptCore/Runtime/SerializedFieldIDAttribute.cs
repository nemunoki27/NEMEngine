namespace NEMEngine;

// serialized field の永続主キーになる Stable Serialized Field GUID を明示する。
// 未指定の場合は source generator が declaringType + serialization origin name から
// 決定的な GUID を生成する（[FormerlySerializedAs] の origin を維持する）。
// 12_editor_scripting_tooling の script template はこの属性で GUID を自動挿入できる。
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false, Inherited = true)]
public sealed class SerializedFieldIDAttribute : Attribute {

    public SerializedFieldIDAttribute(string value) {
        Value = value;
    }

    // 正規化前の GUID 文字列。generator / HostBridge 側で正規化・検証する
    public string Value { get; }
}
