using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	SpriteMove
//============================================================================
public sealed class SpriteMove : ScriptBehaviour
{
    // 移動速度
    [DragSpeed(2.0f)]
    [SerializeField]
    private float moveSpeed = 1.0f;

    //========================================================================
    //	更新フレーム開始処理
    //========================================================================
    public override void Awake()
    {
        Debug.Log("Awake SpriteMove");
    }
    //========================================================================
    //	更新フレーム開始処理
    //========================================================================
    public override void Start()
    {
        Debug.Log("Start SpriteMove");
    }
    //========================================================================
    //	毎フレーム更新処理
    //========================================================================
    public override void Update()
    {
        // 移動
        Move();
    }
    //========================================================================
    //	移動処理
    //========================================================================
    private void Move()
    {

        // 入力をまとめる、xが左右でyが前後
        Vector2 input = Vector2.zero;

        // WASDキー入力
        if (Input.GetKey(KeyCode.W)) { input.y -= 1.0f; }
        if (Input.GetKey(KeyCode.S)) { input.y += 1.0f; }
        if (Input.GetKey(KeyCode.D)) { input.x += 1.0f; }
        if (Input.GetKey(KeyCode.A)) { input.x -= 1.0f; }

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
        // XZ平面のワールド移動方向
        Vector3 moveDirection = new Vector3(input.x, input.y, 0.0f);
        Vector3 direction = Vector3.Normalize(moveDirection);

        // 入力の強さに応じて速度を変えて移動する
        transform.position += direction * (moveSpeed * inputLength * Time.deltaTime);
    }
}