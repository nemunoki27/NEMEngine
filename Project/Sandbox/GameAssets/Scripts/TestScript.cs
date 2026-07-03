using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	TestScript
//============================================================================
public sealed class TestScript : ScriptBehaviour {
	// コンポーネントの参照テスト
	[Label("トランスフォーム")]
	[SerializeField]
	private Transform? transformRef;
	[Label("メッシュ描画")]
	[SerializeField]
	private MeshRenderer? meshRendererRef;
	[Label("フィルメッシュ描画")]
	[SerializeField]
	private FillMeshRenderer? fillMeshRendererRef;

	// エンティティの参照テスト
	[Label("エンティティ")]
	[SerializeField]
	private Entity entityRef;

	// スクリプトの参照テスト
	[Label("テストスクリプト")]
	[SerializeField]
	private RefScriptTest? scriptRef;

	// アセットの参照テスト
	[Label("シーンファイル")]
	[SerializeField]
	private SceneAsset? sceneFileRef;
	[Label("モデルファイル")]
	[SerializeField]
	private Mesh? meshFileRef;

	// リスト参照テスト
	private struct TestStruct {
		public TestStruct() {

		}
		public float value0 = 0.0f;
		public int value1 = 0;
		public string value2 = "HELLO";
	}
	[Label("リストテスト")]
	[SerializeField]
	private List<TestStruct> structs = new();

	//========================================================================
	//	開始時処理
	//========================================================================
	public override void Start() {
		if (meshRendererRef != null) {
			// 設定されたモデルデータをメッシュ描画に設定
			meshRendererRef.Mesh = meshFileRef;
		}
	}

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {
	}
}
