using NEMEngine;
using System.Collections.Generic;

namespace SandboxScripts;

//============================================================================
//	LineDrawTest
//	ラインはLineRendererComponentにAddPointで積み、形状はLineDrawの即時描画で出す
//	パラメータはインスペクターから編集できる
//============================================================================
public sealed class LineDrawTest : ScriptBehaviour {
	// ラインの太さ、ワールド単位
	[SerializeField]
	private float lineThickness = 0.1f;
	// 頂点カラーのグラデーション速度、1で約1秒に1周
	[SerializeField]
	private float gradientSpeed = 0.5f;
	// 隣り合う頂点どうしの色のずれ、大きいほどグラデーションが急になる
	[SerializeField]
	private float gradientPhasePerVertex = 0.15f;

	// Awakeで積んだポリラインの各点。UpdatePointで色を差し替えるため保持する
	private List<LinePoint> linePoints = new List<LinePoint>();
	// 形状の太さ、ワールド単位
	[SerializeField]
	private float shapeThickness = 0.04f;
	// 2D形状の太さ、2Dは画面ピクセル基準なので太め
	[SerializeField]
	private float shape2DThickness = 3.0f;

	// 球
	[SerializeField]
	private Vector3 spherePosition = new Vector3(0.0f, 2.0f, 0.0f);
	[SerializeField]
	private float sphereRadius = 2.0f;
	[SerializeField]
	private Color4 sphereColor = Color4.white;

	// 軸平行ボックス
	[SerializeField]
	private Vector3 aabbMin = new Vector3(3.0f, 0.0f, -1.0f);
	[SerializeField]
	private Vector3 aabbMax = new Vector3(5.0f, 2.0f, 1.0f);
	[SerializeField]
	private Color4 aabbColor = Color4.green;

	// 有向ボックス
	[SerializeField]
	private Vector3 obbCenter = new Vector3(8.0f, 1.0f, 0.0f);
	[SerializeField]
	private Vector3 obbSize = new Vector3(1.0f, 1.0f, 1.0f);
	[SerializeField]
	private Color4 obbColor = Color4.red;

	// 矢印
	[SerializeField]
	private Vector3 arrowPosition = new Vector3(-9.0f, 0.0f, 0.0f);
	[SerializeField]
	private float arrowLength = 3.0f;

	// 軸
	[SerializeField]
	private Vector3 axisPosition = new Vector3(0.0f, 0.0f, 5.0f);
	[SerializeField]
	private float axisLength = 2.0f;

	// 2D円、座標は画面ピクセル(左上原点)
	[SerializeField]
	private Vector2 circleCenter = new Vector2(300.0f, 300.0f);
	[SerializeField]
	private float circleRadius = 100.0f;
	[SerializeField]
	private Color4 circleColor = Color4.green;

	// 2D矩形、座標は画面ピクセル(左上原点)
	[SerializeField]
	private Vector2 rectCenter = new Vector2(650.0f, 300.0f);
	[SerializeField]
	private Vector2 rectSize = new Vector2(250.0f, 180.0f);
	[SerializeField]
	private float rectRotationDegrees = 20.0f;
	[SerializeField]
	private Color4 rectColor = Color4.red;

	//========================================================================
	//	開始処理
	//========================================================================
	public override void Start() {
		// ライン描画用のコンポーネントを確保する、追加は次フレームに反映される
		if (!entity.Has<LineRenderer>()) {
			entity.Add<LineRenderer>();
		}

		// ポリラインを1度だけ積む。AddPointの戻り値はindex付きのLinePointなので保持しておく
		if (entity.TryGet<LineRenderer>(out LineRenderer line)) {
			line.Clear();
			linePoints = new List<LinePoint> {
				line.AddPoint(new Vector3(-6.0f, 0.0f, 0.0f), Color4.red, lineThickness),
				line.AddPoint(new Vector3(-6.0f, 4.0f, 0.0f), Color4.red, lineThickness),
				line.AddPoint(new Vector3(-2.0f, 4.0f, 0.0f), Color4.green, lineThickness),
				line.AddPoint(new Vector3(-2.0f, 0.0f, 0.0f), Color4.blue, lineThickness),
			};
		}
	}

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {

		// Awakeで積んだ各頂点の色を時間で進める。indexごとに位相をずらして色を流す
		if (linePoints.Count > 0 && entity.TryGet<LineRenderer>(out LineRenderer line)) {
			float time = (float)Time.TimeSinceStartup;
			for (int i = 0; i < linePoints.Count; ++i) {
				// LinePointはstructなので一旦取り出して書き換え、Listへ書き戻す
				LinePoint point = linePoints[i];
				point.color = Rainbow(time * gradientSpeed + i * gradientPhasePerVertex);
				linePoints[i] = point;
				// 保持しているindexの点だけを差し替える、座標と太さはAwakeのまま
				line.UpdatePoint(point);
			}
		}

		Quaternion noRotation = Quaternion.FromEulerDegrees(new Vector3(0.0f, 0.0f, 0.0f));
		// 3D形状の即時描画
		LineDraw.DrawSphere(spherePosition, sphereRadius, sphereColor, 8, shapeThickness);
		LineDraw.DrawAABB(aabbMin, aabbMax, aabbColor, shapeThickness);
		LineDraw.DrawOBB(obbCenter, obbSize, noRotation, obbColor, shapeThickness);
		LineDraw.DrawArrow(arrowPosition, arrowLength, noRotation, Color4.white, shapeThickness);
		LineDraw.DrawAxis(axisPosition, noRotation, axisLength, shapeThickness);

		// 2D形状の即時描画、座標と太さは画面ピクセル基準
		LineDraw.DrawCircle(circleCenter, circleRadius, circleColor, 24, shape2DThickness);
		LineDraw.DrawRect(rectCenter, rectSize, rectColor, rectRotationDegrees, shape2DThickness);
	}

	//========================================================================
	//	位相を虹色へ変換する
	//	RGBを120度ずつ位相をずらしたsin波で出し、phaseが1進むと一周する
	//========================================================================
	private static Color4 Rainbow(float phase) {
		float angle = phase * Math.pi * 2.0f;
		float r = 0.5f + 0.5f * Math.Sin(angle);
		float g = 0.5f + 0.5f * Math.Sin(angle + Math.pi * 2.0f / 3.0f);
		float b = 0.5f + 0.5f * Math.Sin(angle + Math.pi * 4.0f / 3.0f);
		float a = 0.5f + 0.5f * Math.Sin(angle + Math.pi * 4.0f / 3.0f); ;
		return new Color4(r, g, b, a);
	}
}
