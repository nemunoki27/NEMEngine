namespace NEMEngine;

// field rename 時の migration 用。旧 serialization 名を保持する。
// [FormerlyKnownScriptType]（script 型 rename）とは責務が異なり、
// こちらは field 単位の rename を扱う。複数指定可能。
[AttributeUsage(AttributeTargets.Field, AllowMultiple = true, Inherited = true)]
public sealed class FormerlySerializedAsAttribute : Attribute {

    public FormerlySerializedAsAttribute(string oldName) {
        OldName = oldName;
    }

    // 旧 serialization 名（field 名）。Stable Field GUID 解決に失敗した legacy data の救済に使う
    public string OldName { get; }
}
