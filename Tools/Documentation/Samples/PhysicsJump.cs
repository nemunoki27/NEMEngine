using NEMEngine;

public sealed class PhysicsJump : MonoBehaviour {

    [SerializeField, Min(0.0f)] private float jumpImpulse = 5.0f;
    private Rigidbody? body;
    private bool jumpRequested;

    void Start() {
        // 物理更新で使う参照を保持
        body = GetComponent<Rigidbody>();
    }

    void Update() {
        // 短いキー入力を次の固定更新まで保持
        if (Input.GetKeyDown(KeyCode.Space)) jumpRequested = true;
    }

    void FixedUpdate() {
        // 1回の入力につき1回だけ上向きの衝撃を加える
        if (!jumpRequested || body == null) return;
        jumpRequested = false;
        body.AddForce(new Vector3(0.0f, jumpImpulse, 0.0f), ForceMode.Impulse);
    }
}
