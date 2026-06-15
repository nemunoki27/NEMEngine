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
    // 進行方向への回転補間の速さ、大きいほど素早く向く
    [SerializeField]
    private float rotationLerpRate = 12.0f;
    // 左スティックのデッドゾーン
    [SerializeField]
    private float stickDeadZone = 0.2f;

    // 発生させるPrefab
    [SerializeField]
    private AssetRef<PrefabAsset> sphere;

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
        // オブジェクトの発生
        CreateCube();
        // プレイヤーの移動
        Move();
    }

    //========================================================================
    //	オブジェクトの発生
    //========================================================================
    private void CreateCube()
    {
        // スペース入力でその場にオブジェクトを作成
        if (Input.GetKeyDown(KeyCode.Space))
        {
            // Prefabが未割り当てなら何もしない
            if (sphere.IsNull)
            {
                return;
            }
            // プレイヤーの足元へ現在の向きで生成する
            Prefab.Instantiate(sphere, transform.position, transform.rotation);
        }
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

        // XZ平面のワールド移動方向
        Vector3 moveDirection = new Vector3(input.x, 0.0f, input.y);
        Vector3 direction = Vector3.Normalize(moveDirection);

        // 入力の強さに応じて速度を変えて移動する
        transform.position += direction * (moveSpeed * inputLength * Time.deltaTime);

        // 進行方向へY軸ヨーを滑らかに補間して向く
        float yawDegree = Math.RadToDeg(Math.Atan2(direction.x, direction.z));
        Quaternion target = Quaternion.FromEulerDegrees(new Vector3(0.0f, yawDegree, 0.0f));
        float lerpRate = 1.0f - Math.Exp(-rotationLerpRate * Time.deltaTime);
        transform.rotation = Quaternion.Slerp(transform.rotation, target, lerpRate);
    }
}