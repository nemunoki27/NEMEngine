using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	WorldToScreenPointTest
//============================================================================
[DefaultExecutionOrder(1000)]
public sealed class WorldToScreenPointTest : ScriptBehaviour {

	[SeparatorText("追従設定")]
	[Label("3D追従対象")]
	[SerializeField]
	private Entity target;

	[Label("表示Sprite")]
	[SerializeField]
	private SpriteRenderer? marker;

	[Label("Canvas")]
	[SerializeField]
	private Canvas? canvas;

	[Label("ワールドオフセット")]
	[SerializeField]
	private Vector3 worldOffset = new(0.0f, 1.5f, 0.0f);

	[Label("カメラ後方で非表示")]
	[SerializeField]
	private bool hideBehindCamera = true;

	//========================================================================
	//	開始時処理
	//========================================================================
	public override void Start() {

		if (marker == null || canvas == null) {
			Debug.LogError("[WorldToScreenPointTest] 表示SpriteとCanvasを設定してください");
			return;
		}
		if (marker.entity.parent != canvas.entity) {
			Debug.LogError("[WorldToScreenPointTest] 表示SpriteをCanvasの直下に配置してください");
			return;
		}

		Debug.Log("[WorldToScreenPointTest] 3D追従を開始します、F6でVector2とVector3の結果を表示します");
	}

	//========================================================================
	//	毎フレーム後更新処理
	//========================================================================
	public override void LateUpdate() {

		if (Input.GetKeyDown(KeyCode.F6)) {
			OutputScreenPosition();
		}

		SpriteRenderer? currentMarker = marker;
		Canvas? currentCanvas = canvas;
		if (!target.isAlive || currentMarker == null || currentCanvas == null ||
			currentMarker.entity.parent != currentCanvas.entity) {
			SetMarkerVisible(false);
			return;
		}

		const float cameraFront = 0.0f;
		Vector3 worldPosition = target.transform.position + worldOffset;

		if (!Camera.TryWorldToScreenPoint(worldPosition, out Vector3 screenPosition) ||
			(hideBehindCamera && screenPosition.z <= cameraFront)) {
			currentMarker.Visible = false;
			return;
		}
		if (!currentCanvas.TryScreenToLocalPoint(
			new Vector2(screenPosition.x, screenPosition.y), out Vector2 localPosition)) {
			currentMarker.Visible = false;
			return;
		}

		currentMarker.Visible = true;
		Vector3 markerPosition = currentMarker.transform.localPosition;
		currentMarker.transform.localPosition =
			new Vector3(localPosition.x, localPosition.y, markerPosition.z);
	}

	// 表示状態を設定する
	private void SetMarkerVisible(bool visible) {

		if (marker != null) {
			marker.Visible = visible;
		}
	}

	// Vector2とVector3の変換結果を表示する
	private void OutputScreenPosition() {

		if (!target.isAlive) {
			Debug.Log("[WorldToScreenPointTest] 3D追従対象が無効です");
			return;
		}

		Vector3 worldPosition = target.transform.position + worldOffset;
		Vector3 screenPosition3D = Camera.WorldToScreenPoint(worldPosition);
		Vector2 screenPosition2D = Camera.WorldToScreenPoint2D(worldPosition);
		Debug.Log($"[WorldToScreenPointTest] Vector3={screenPosition3D}, Vector2={screenPosition2D}");
	}
}
