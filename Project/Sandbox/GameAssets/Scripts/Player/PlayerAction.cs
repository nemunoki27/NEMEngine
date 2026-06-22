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

    // プレファブ発生オフセットY
    [SerializeField]
    private float createPrefabPosY = 0.0f;
    // 発生させるPrefab
    [SerializeField]
    private AssetRef<PrefabAsset> prefab;

    // 移動の正面に使うカメラEntity、未設定ならワールド軸基準で移動する
    [SerializeField]
    private EntityRef cameraEntity;
    // 解決済みカメラのキャッシュ、EntityRef探索を毎フレーム行わない
    private Entity cachedCamera;

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
            if (prefab.IsNull)
            {
                return;
            }
            Vector3 createPos = transform.position;
            createPos.y = createPrefabPosY;

            // プレイヤーの足元へ現在の向きで生成する
            Prefab.Instantiate(prefab, createPos, transform.rotation);
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