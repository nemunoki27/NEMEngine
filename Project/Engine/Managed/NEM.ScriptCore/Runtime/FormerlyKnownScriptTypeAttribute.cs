namespace NEMEngine;

// script class / namespaceのrename前の完全修飾名を記録しStable Script Type GUIDを維持する
// field名のrenameには[FormerlySerializedAs]を使う
[AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = false)]
public sealed class FormerlyKnownScriptTypeAttribute : Attribute {

	public FormerlyKnownScriptTypeAttribute(string fullTypeName) {
		FullTypeName = fullTypeName;
	}

	// rename前の完全修飾型名
	public string FullTypeName { get; }
}
