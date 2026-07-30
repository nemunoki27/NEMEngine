namespace NEMEngine;

// field rename前の名前を記録しStable Field GUIDを維持する
// script型のrenameには[FormerlyKnownScriptType]を使う
[AttributeUsage(AttributeTargets.Field, AllowMultiple = true, Inherited = true)]
public sealed class FormerlySerializedAsAttribute : Attribute {

    public FormerlySerializedAsAttribute(string oldName) {
        OldName = oldName;
    }

    // rename前のfield名
    public string OldName { get; }
}
