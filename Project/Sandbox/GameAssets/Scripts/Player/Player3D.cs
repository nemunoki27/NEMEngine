using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using NEMEngine;

namespace GJ4Scripts;

//============================================================================
//	Player
//============================================================================
public sealed class Player3D : MonoBehaviour {

	[Min(0.0f)]
	[Label("移動速度")]
	[SerializeField]
	private float moveSpeed = 0.0f;

	[Min(0.0f)]
	[Label("ジャンプ力")]
	[SerializeField]
	private float jumpForce = 10.0f;

	[Min(0.0f)]
	[Label("左スティック閾値")]
	[SerializeField]
	private float stickDeadZone = 0.2f;

	// 物理コンポーネント
	private Rigidbody? rigidbody = null;
	// 衝突相手のエンティティ
	private readonly HashSet<GameObject> groundContacts = new();
	// ジャンプ入力されたか
	private bool jumpRequested = false;

	//========================================================================
	//	開始時処理
	//========================================================================
	private void Start() {

		rigidbody = GetComponent<Rigidbody>();
	}

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	private void Update() {

		// ジャンプ入力を受け付け
		if (Input.GetGamepadButtonDown(0, GamepadButton.A) || Input.GetKeyDown(KeyCode.Space)) {
			jumpRequested = true;
		}
	}

	//========================================================================
	//	毎後フレーム更新処理
	//========================================================================
	private void FixedUpdate() {

		// 移動処理
		// ジャンプ
		Jump();
		// X移動のみ
		Move();
	}

	//========================================================================
	//	ジャンプ処理
	//========================================================================
	private void Jump() {

		// ジャンプ入力されていないときのみ
		if (rigidbody == null || !jumpRequested) {
			return;
		}
		jumpRequested = false;
		// 地面と触れていなければジャンプ不可
		if (groundContacts.Count == 0) {
			return;
		}
		// ジャンプ力を加算
		rigidbody.AddForce(new Vector3(0.0f, jumpForce, 0.0f), ForceMode.Impulse);
		// 接触エンティティデータクリア
		groundContacts.Clear();
	}

	//========================================================================
	//	移動処理
	//========================================================================
	private void Move() {

		if (rigidbody == null) {
			return;
		}
		// 地面にいるときのみ

		// 移動の入力を取る
		Vector2 input = new Vector2(
			Input.GetGamepadAxis(0, GamepadAxis.LeftStickX),
			Input.GetGamepadAxis(0, GamepadAxis.LeftStickY)
		);
		if (Input.GetKey(KeyCode.A)) {

			input.x = -1;
		} else if (Input.GetKey(KeyCode.D)) {

			input.x = 1;
		}

		// 閾値以下なら入力を0.0fにする
		if (input.magnitude <= stickDeadZone) {
			input = Vector2.zero;
		}

		Vector3 velocity = rigidbody.LinearVelocity;
		velocity.x = input.x * moveSpeed;
		rigidbody.LinearVelocity = velocity;
	}

	//========================================================================
	//	衝突処理
	//========================================================================
	private void OnCollisionEnter(Collision collision) {

		// 衝突したら対象を登録
		RegisterGroundContact(collision);
	}

	private void OnCollisionStay(Collision collision) {

		// 衝突したら対象を登録
		RegisterGroundContact(collision);
	}

	private void OnCollisionExit(Collision collision) {

		// 離れたら削除
		groundContacts.Remove(collision.gameObject);
	}

	private void RegisterGroundContact(Collision collision) {

		if (rigidbody == null) {
			return;
		}

		if (collision.isTrigger) {
			return;
		}
		// ジャンプしていないとき
		if (0.01f < rigidbody.LinearVelocity.y) {
			return;
		}
		groundContacts.Add(collision.gameObject);
	}
}
