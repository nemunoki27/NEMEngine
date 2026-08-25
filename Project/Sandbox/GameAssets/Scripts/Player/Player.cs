using System.Collections.Generic;
using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	Player
//============================================================================
public sealed class Player : ScriptBehaviour {

	[SerializeField]
	[Label("移動速度")]
	[Min(0.0f)]
	private float moveSpeed = 320.0f;

	[SerializeField]
	[Label("ジャンプ速度")]
	[Min(0.0f)]
	private float jumpSpeed = 500.0f;

	[SerializeField]
	[Label("重力倍率")]
	[Min(0.0f)]
	private float gravityScale = 80.0f;

	[SerializeField]
	[Label("コヨーテタイム")]
	[Min(0.0f)]
	private float coyoteTime = 0.1f;

	[SerializeField]
	[Label("ジャンプ入力猶予")]
	[Min(0.0f)]
	private float jumpBufferTime = 0.1f;

	[SerializeField]
	[Label("接地法線の下限")]
	[Range(0.0f, 1.0f)]
	private float groundNormalThreshold = 0.6f;

	private readonly HashSet<Entity> groundContacts = new();
	private Rigidbody2D? rigidbody;
	private float horizontalInput;
	private float coyoteTimer;
	private float jumpBufferTimer;

	//========================================================================
	//	初期化処理
	//========================================================================
	public override void Awake() {

		rigidbody = GetComponent<Rigidbody2D>();
		if (rigidbody == null) {
			Debug.LogError("Playerの移動にはRigidbody2Dが必要です");
			Enabled = false;
			return;
		}

		// キャラクターが衝突で傾かないように回転を固定する
		rigidbody.FreezeRotation = true;
		rigidbody.UseGravity = true;
		rigidbody.GravityScale = gravityScale;
	}

	//========================================================================
	//	入力更新処理
	//========================================================================
	public override void Update() {

		horizontalInput = 0.0f;
		if (Input.GetKey(KeyCode.A)) {
			horizontalInput -= 1.0f;
		}
		if (Input.GetKey(KeyCode.D)) {
			horizontalInput += 1.0f;
		}

		if (Input.GetKeyDown(KeyCode.W)) {
			jumpBufferTimer = Math.Max(jumpBufferTime, Time.FixedDeltaTime);
		}
	}

	//========================================================================
	//	物理更新処理
	//========================================================================
	public override void FixedUpdate() {

		if (rigidbody == null) {
			return;
		}

		const float minimumTimer = 0.0f;
		if (groundContacts.Count != 0) {
			coyoteTimer = coyoteTime;
		} else {
			coyoteTimer = Math.Max(minimumTimer, coyoteTimer - Time.FixedDeltaTime);
		}
		jumpBufferTimer = Math.Max(minimumTimer, jumpBufferTimer - Time.FixedDeltaTime);

		Vector2 velocity = rigidbody.LinearVelocity;
		velocity.x = horizontalInput * moveSpeed;

		if (minimumTimer < jumpBufferTimer && minimumTimer < coyoteTimer) {

			// 2Dスクリーン座標はY+が下向きなので、負方向へ跳ね上げる
			velocity.y = -jumpSpeed;
			jumpBufferTimer = minimumTimer;
			coyoteTimer = minimumTimer;
			groundContacts.Clear();
		}
		rigidbody.LinearVelocity = velocity;
	}

	//========================================================================
	//	接地判定
	//========================================================================
	public override void OnCollisionEnter(Collision collision) {

		RegisterGroundContact(collision);
	}

	public override void OnCollisionStay(Collision collision) {

		RegisterGroundContact(collision);
	}

	public override void OnCollisionExit(Collision collision) {

		groundContacts.Remove(collision.entity);
	}

	public override void OnDisable() {

		horizontalInput = 0.0f;
		coyoteTimer = 0.0f;
		jumpBufferTimer = 0.0f;
		groundContacts.Clear();
	}

	private void RegisterGroundContact(Collision collision) {

		if (collision.isTrigger || collision.normal.y < groundNormalThreshold) {
			return;
		}
		// ジャンプ直後に残った接触を接地として再登録しない
		if (rigidbody != null && rigidbody.LinearVelocity.y < -0.01f) {
			return;
		}
		groundContacts.Add(collision.entity);
	}
}
