namespace NEMEngine;

// script class / namespace の旧 full type name を記録する（class/namespace rename 対応）。
// legacy scene / prefab の type 文字列を Stable Script Type GUID へ移行する際の照合に使う。
// 注意: フィールド名変更用の [FormerlySerializedAs]（05）とは別物。
//   [FormerlySerializedAs] : フィールド名変更
//   [FormerlyKnownScriptType] : script class / namespace 名変更
[AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = false)]
public sealed class FormerlyKnownScriptTypeAttribute : Attribute {

	public FormerlyKnownScriptTypeAttribute(string fullTypeName) {
		FullTypeName = fullTypeName;
	}

	// 旧 full type name（"OldNamespace.OldClass" 形式）
	public string FullTypeName { get; }
}
