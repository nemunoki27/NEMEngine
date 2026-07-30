namespace NEMEngine;

// 1 つの concrete ScriptBehaviour 型のメタデータ。
// Roslyn source generator が compile 時に GeneratedScriptManifest として埋め込み、
// HostBridge が load 時に読み取って GUID ベースの登録・manifest 生成に使う。
public readonly struct ScriptTypeDescriptor {

	public ScriptTypeDescriptor(
		string scriptTypeId,
		string fullTypeName,
		string displayName,
		string sourcePath,
		bool hasExplicitId) {

		ScriptTypeId = scriptTypeId;
		FullTypeName = fullTypeName;
		DisplayName = displayName;
		SourcePath = sourcePath;
		HasExplicitId = hasExplicitId;
	}

	// 正規化済み Stable Script Type GUID
	public string ScriptTypeId { get; }
	// 完全修飾型名
	public string FullTypeName { get; }
	// 表示名
	public string DisplayName { get; }
	// 定義元 .cs のパス（drag&drop の source 照合用。永続識別には使わない）
	public string SourcePath { get; }
	// [ScriptTypeId]またはmetadataから安定IDを解決できたか
	public bool HasExplicitId { get; }
}
