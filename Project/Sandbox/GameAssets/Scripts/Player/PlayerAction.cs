using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	PlayerAction
//============================================================================
public sealed class PlayerAction : ScriptBehaviour
{
    // 移動速度
    [SerializeField]
    private float moveSpeed = 5.0f;
    // 進行方向への回転補間の速さ
    [SerializeField]
    private float rotationLerpRate = 12.0f;
    // 左スティックのデッドゾーン
    [SerializeField]
    private float stickDeadZone = 0.2f;

    // ジャンプの強さ、Rigidbodyへ上方向の瞬間的な力として加える
    [SerializeField]
    private float jumpForce = 6.0f;
    // 接地猶予、地面を離れてからこの秒数はジャンプを受け付ける
    [SerializeField]
    private float coyoteTime = 0.1f;

    // 移動の正面に使うカメラEntity、未設定ならワールド軸基準で移動する
    [SerializeField]
    private EntityRef cameraEntity;
    // 解決済みカメラのキャッシュ、EntityRef探索を毎フレーム行わない
    private Entity cachedCamera;

    // ジャンプ入力のフラグ、Updateで拾いFixedUpdateで消費する
    private bool jumpRequested;
    // 接地の残り猶予秒数、0より大きければ接地中とみなす
    private float groundedTimer;

    //========================================================================
    //	更新フレーム開始処理
    //========================================================================
    public override void Awake()
    {
        Debug.Log("Awake PlayerAction");
    }
    //========================================================================
    //	更新フレーム開始処理
    //========================================================================
    public override void Start()
    {
        Debug.Log("Start PlayerAction");
    }
    //========================================================================
    //	毎フレーム更新処理
    //========================================================================
    public override void Update()
    {
        // プレイヤーの移動
        Move();
        // ジャンプ入力の取得
        ReadJumpInput();
    }
    //========================================================================
    //	物理更新、ジャンプの適用と接地猶予の減衰
    //========================================================================
    public override void FixedUpdate()
    {
        // 接地猶予を減らす、接地中は衝突コールバックが毎ステップ補充する
        if (groundedTimer > 0.0f)
        {
            groundedTimer -= Time.fixedDeltaTime;
        }

        // 接地中にジャンプ要求があれば上方向へ瞬間的な力を加える
        if (jumpRequested && groundedTimer > 0.0f)
        {
            // Rigidbodyが無ければジャンプできない、プレイヤーにRigidbodyとCollisionが必要
            if (TryGet<Rigidbody>(out Rigidbody body))
            {
                body.AddForce(Vector3.up * jumpForce, ForceMode.Impulse);
            }
            // 多段ジャンプを防ぐため接地状態を消費する
            groundedTimer = 0.0f;
        }
        jumpRequested = false;
    }
    //========================================================================
    //	衝突開始と継続で接地を判定する
    //========================================================================
    public override void OnCollisionEnter(Collision collision)
    {

        if (collision.entity.CompareTag(""))
        {

        }

        UpdateGrounded(collision);
    }
    public override void OnCollisionStay(Collision collision)
    {
        UpdateGrounded(collision);
    }

    //========================================================================
    //	移動処理
    //========================================================================
    private void Move()
    {

        // 入力をまとめる、xが左右でyが前後
        Vector2 input = Vector2.zero;

        // WASDキー入力
        if (Input.GetKey(KeyCode.W)) { input.y += 1.0f; }
        if (Input.GetKey(KeyCode.S)) { input.y -= 1.0f; }
        if (Input.GetKey(KeyCode.D)) { input.x += 1.0f; }
        if (Input.GetKey(KeyCode.A)) { input.x -= 1.0f; }

        // ゲームパッド左スティックがデッドゾーンを超えたか
        Vector2 stick = Input.leftStick;
        if (stickDeadZone < stick.length)
        {
            input.x += stick.x;
            input.y += stick.y;
        }

        // 斜め入力の正規化
        float inputLength = input.length;
        if (1.0f < inputLength)
        {
            input = input.normalized;
            inputLength = 1.0f;
        }
        // 入力が無ければ処理しない
        if (inputLength <= 0.0001f)
        {
            return;
        }

        // カメラの向きを正面として入力を回す、カメラが無ければワールド軸そのまま
        Vector3 forward = new Vector3(0.0f, 0.0f, 1.0f);
        Vector3 right = new Vector3(1.0f, 0.0f, 0.0f);
        Entity camera = GetCamera();
        if (camera.isAlive)
        {
            Transform camTransform = camera.transform;
            // 地面移動なので前方と右方向を水平化して使う
            forward = camTransform.forward;
            right = camTransform.right;
            forward.y = 0.0f;
            right.y = 0.0f;
            forward = Vector3.Normalize(forward);
            right = Vector3.Normalize(right);
        }

        // カメラ相対のXZ移動方向
        Vector3 moveDirection = right * input.x + forward * input.y;
        Vector3 direction = Vector3.Normalize(moveDirection);

        // 入力の強さに応じて速度を変えて移動する
        transform.position += direction * (moveSpeed * inputLength * Time.deltaTime);

        // 進行方向へY軸ヨーを滑らかに補間して向く
        float yawDegree = Math.RadToDeg(Math.Atan2(direction.x, direction.z));
        Quaternion target = Quaternion.FromEulerDegrees(new Vector3(0.0f, yawDegree, 0.0f));
        float lerpRate = 1.0f - Math.Exp(-rotationLerpRate * Time.deltaTime);
        transform.rotation = Quaternion.Slerp(transform.rotation, target, lerpRate);
    }

    //========================================================================
    //	ジャンプ入力の取得
    //========================================================================
    private void ReadJumpInput()
    {
        // スペースかゲームパッドAでジャンプ要求を立てる、適用はFixedUpdateで行う
        if (Input.GetKeyDown(KeyCode.Space) || Input.GetGamepadButtonDown(GamepadButton.A))
        {
            jumpRequested = true;
        }
    }
    //========================================================================
    //	接地判定の更新
    //========================================================================
    private void UpdateGrounded(Collision collision)
    {
        // 接触点がプレイヤー中心より下なら床と判断して接地猶予を補充する
        if (collision.point.y < transform.position.y)
        {
            groundedTimer = coyoteTime;
        }
    }
    //========================================================================
    //	カメラEntityの取得
    //========================================================================
    private Entity GetCamera()
    {
        if (!cachedCamera.isAlive)
        {
            cachedCamera = cameraEntity.Resolve();
        }
        return cachedCamera;
    }
}