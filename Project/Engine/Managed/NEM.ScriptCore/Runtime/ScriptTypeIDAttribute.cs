namespace NEMEngine;

// ScriptBehaviour 派生型へ付与する安定 Script Type GUID。
// ファイル名 / クラス名 / namespace / 列挙順 / runtime type index に依存しない永続識別子。
// rename・namespace 変更・ファイル移動でも不変であること。
[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
public sealed class ScriptTypeIDAttribute : Attribute {

	public ScriptTypeIDAttribute(string value) {
		Value = value;
	}

	// 正規化前の GUID 文字列（"8b46e04d-934e-437b-a6ff-d9bc24ec7f91" 形式）
	public string Value { get; }
}
