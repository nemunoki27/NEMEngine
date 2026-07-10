using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	CreateCircleFillMesh
//============================================================================
public sealed class CreateCircleFillMesh : ScriptBehaviour {

	[SerializeField]
	private float radius = 4.0f;
	[SerializeField]
	private int division = 8;
	[SerializeField]
	private bool buildMesh = false;

	private FillMeshRenderer? meshRenderer;

	//========================================================================
	//	開始時処理
	//========================================================================
	public override void Start() {

		meshRenderer = GetComponent<FillMeshRenderer>();
	}

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {

		// メッシュ構築
		if (!buildMesh) {
			return;
		}

		if (meshRenderer == null) {
			return;
		}

		int div = Math.Max(3, division);

		// 円形に分割
		List<Vector3> points = new List<Vector3>();

		for (int i = 0; i < div; ++i) {

			float angle = MathF.PI * 2.0f * i / div;

			float pointX = MathF.Cos(angle) * radius;
			float pointZ = MathF.Sin(angle) * radius;

			points.Add(new Vector3(pointX, 0.0f, pointZ));
		}

		// ビルド依頼
		meshRenderer.SetFacePositions(points.ToArray());
		meshRenderer.BuildMesh = true;

		buildMesh = false;
	}
}