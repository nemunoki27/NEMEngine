using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	MeshMaterialTest
//============================================================================
public sealed class MeshMaterialTest : MonoBehaviour {

	[SerializeField]
	private float strength = 0.0f;

	private MaterialParameterID id = MaterialParameterID.FromHex("e1e686f24797ab03");

	private MeshRenderer? meshRenderer;

	//========================================================================
	//	開始時処理
	//========================================================================
	private void Start() {

		meshRenderer = GetComponent<MeshRenderer>();
	}

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	private void Update() {

		if (meshRenderer == null) {
			return;
		}

		MaterialInstance material = meshRenderer.GetMaterialInstance(0);
		material.SetFloat(id, "strength", strength);
	}
}