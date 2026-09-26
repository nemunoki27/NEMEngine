namespace NEMEngine;

// 1 つの concrete MonoBehaviour 型のメタデータ。
// Roslyn source generator が compile 時に GeneratedScriptManifest として埋め込み、
// HostBridge が load 時に読み取って GUID ベースの登録・manifest 生成に使う。
public readonly struct ScriptTypeDescriptor {

	public ScriptTypeDescriptor(
		string scriptTypeID,
		string fullTypeName,
		string displayName,
		string sourcePath,
		bool hasExplicitID) {

		ScriptTypeID = scriptTypeID;
		FullTypeName = fullTypeName;
		DisplayName = displayName;
		SourcePath = sourcePath;
		HasExplicitID = hasExplicitID;
	}

	// 正規化済み Stable Script Type GUID
	public string ScriptTypeID { get; }
	// 完全修飾型名
	public string FullTypeName { get; }
	// 表示名
	public string DisplayName { get; }
	// 定義元 .cs のパス（drag&drop の source 照合用。永続識別には使わない）
	public string SourcePath { get; }
	// [ScriptTypeID]またはmetadataから安定IDを解決できたか
	public bool HasExplicitID { get; }
}
