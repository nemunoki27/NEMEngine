using NEMEngine;
using System.Collections.Generic;

namespace SandboxScripts;

//============================================================================
//	FillMeshCircleTest
//============================================================================
public sealed class FillMeshCircleTest : ScriptBehaviour {

	// 円の分割数
	[SerializeField]
	private int divisions = 32;
	// 円の半径
	[SerializeField]
	private float radius = 2.0f;
	// メッシュ構築フラグ
	[SerializeField]
	private bool build = false;
	// 面の色
	[SerializeField]
	private Color4 fillColor = Color4.white;

	// 円状の点
	[SerializeField]
	private List<Vector3> circlePoints = new List<Vector3>();

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {

		if (!build) {
			return;
		}
		build = false;
		if (!entity.TryGet<FillMeshRenderer>(out FillMeshRenderer fill)) {
			return;
		}

		// 点をクリア
		circlePoints.Clear();
		// 円上に点を配置
		for (int i = 0; i < divisions; ++i) {

			// i番目の角度、位置
			float angle = (float)(System.Math.PI * 2.0 / divisions * i);
			float cos = Math.Cos(angle) * radius;
			float sin = Math.Sin(angle) * radius;

			circlePoints.Add(new Vector3(cos, 0.0f, sin));
		}
	}
}