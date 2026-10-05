using NEMEngine;

namespace __GAME_NAME__Scripts;

// キー入力で移動するサンプル
public sealed class SampleMover : MonoBehaviour {

	public float speed = 1.0f;

	[SerializeField]
	private Vector3 direction = new(1.0f, 0.0f, 0.0f);

	[SerializeField]
	private bool logDebugInput = true;

	[SerializeField]
	private Vector2 mouseLogOffset = Vector2.zero;

	private Vector3 initialLocalPosition;

	private void Start() {

		initialLocalPosition = transform.localPosition;
		Debug.Log($"SampleMover Start gameObject={gameObject.name} localPosition={initialLocalPosition}");
	}

	private void Update() {

		// 入力と経過時間から移動する
		float moveSpeed = Input.GetKey(KeyCode.LeftShift) ? speed * 3.0f : speed;
		Vector3 position = transform.localPosition;
		position += direction * (moveSpeed * Time.deltaTime);
		transform.localPosition = position;

		if (!logDebugInput) {
			return;
		}

		if (Input.GetKeyDown(KeyCode.Space)) {
			Debug.Log($"Space pressed gameObject={gameObject.name} localPosition={transform.localPosition}");
		}
		if (Input.GetKeyDown(KeyCode.R)) {
			transform.localPosition = initialLocalPosition;
			Debug.Log($"Reset localPosition gameObject={gameObject.name} localPosition={transform.localPosition}");
		}
		if (Input.GetMouseButtonDown(0)) {
			Debug.Log($"Left mouse position={Input.mousePosition + mouseLogOffset}");
		}
	}
}
