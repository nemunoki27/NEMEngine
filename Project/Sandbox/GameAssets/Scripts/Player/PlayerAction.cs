using System.Collections;
using System.Collections.Generic;
using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	PlayerAction
//============================================================================
public sealed class PlayerAction : ScriptBehaviour
{
    // 移動速度
    [SeparatorText("移動")]
    [SerializeField]
    [Label("移動速度")]
    private float moveSpeed = 5.0f;
    // 進行方向への回転補間の速さ
    [SerializeField]
    [Label("回転補間レート")]
    private float rotationLerpRate = 12.0f;
    // 左スティックのデッドゾーン
    [SerializeField]
    [Label("スティック感度")]
    private float stickDeadZone = 0.2f;

    // ジャンプの強さ、Rigidbodyへ上方向の瞬間的な力として加える
    [SeparatorText("ジャンプ")]
    [SerializeField]
    [Label("ジャンプ力")]
    private float jumpForce = 6.0f;
    // 接地猶予、地面を離れてからこの秒数はジャンプを受け付ける
    [SerializeField]
    [Label("接地猶予")]
    private float coyoteTime = 0.1f;

    // 移動の正面に使うカメラEntity、未設定ならワールド軸基準で移動する
    [SerializeField]
    [Label("カメラ")]
    private EntityRef cameraEntity;
    // 解決済みカメラのキャッシュ、EntityRef探索を毎フレーム行わない
    private Entity cachedCamera;

    // ジャンプ入力のフラグ、Updateで拾いFixedUpdateで消費する
    private bool jumpRequested;
    // 接地の残り猶予秒数、0より大きければ接地中とみなす
    private float groundedTimer;

    // 最大HP、Enemyタグに当たるたびに1減る
    [SeparatorText("ステータス")]
    [SerializeField]
    [Label("最大HP")]
    [Range(1.0f, 99.0f)]
    [Tooltip("最大HP。被弾で1ずつ減り0でゲームオーバー")]
    private int maxHP = 5;
    // 被弾後の無敵時間、敵と離脱再接触を繰り返しても1接触1ダメージに抑える
    [SerializeField]
    [Label("無敵時間")]
    [DragSpeed(0.1f)]
    [Tooltip("被弾後この秒数は再被弾しない。敵に触れ続けた連続被弾を防ぐ")]
    private float invincibleTime = 0.5f;

    // HP表示用のTextRendererを持つEntity、現在HP / 最大HP を表示する
    [SeparatorText("UI")]
    [SerializeField]
    [Label("HP表示テキスト")]
    [Tooltip("現在HP / 最大HP を表示するTextRendererを持つEntity")]
    private EntityRef hpTextEntity;

    // HPが0になったら遷移するシーン
    [SeparatorText("シーン遷移")]
    [SerializeField]
    [Label("ゲームオーバーシーン")]
    [Tooltip("HPが0になったとき読み込むゲームオーバーシーン")]
    private AssetRef<SceneAsset> gameOverScene;

    // 9キーで自分の位置に生成するプレファブ
    [SeparatorText("プレファブ生成")]
    [SerializeField]
    [Label("生成プレファブ")]
    [Tooltip("9キーで自分の位置に生成するプレファブ")]
    private AssetRef<PrefabAsset> spawnPrefab;
    // プレファブ生成位置の高さオフセット
    [SerializeField]
    [Label("生成高さオフセット")]
    [DragSpeed(0.1f)]
    [Tooltip("プレファブ生成位置の高さオフセット")]
    private float spawnHeightOffset = 1.0f;

    // List動作確認用、起動時に中身をログ出力し実行中は増減を繰り返す
    [SeparatorText("動作確認")]
    [SerializeField]
    [Label("確認用リスト")]
    [Tooltip("起動時に走査してListのシリアライズと反復を確認する数値リスト")]
    private List<int> testNumbers = new() { 10, 20, 30 };

    // スクリプト間参照の確認用、被弾時にスコアを加算する別スクリプトへの参照
    [SerializeField]
    [Label("スコア管理")]
    [Tooltip("被弾時にAddScoreを呼ぶScoreKeeperへの参照")]
    private ScriptRef<ScoreKeeper> scoreKeeper;

    // コンポーネント参照の動作確認、HP表示のTextRendererを直接参照する
    [SerializeField]
    [Label("HP表示(コンポーネント参照)")]
    [Tooltip("EntityRef+GetComponentを使わずTextRendererを直接参照する確認用")]
    private ComponentRef<TextRenderer> hpTextComponent;

    // 停止時に再生する待機アニメーションのクリップ名
    [SeparatorText("アニメーション")]
    [SerializeField]
    [Label("待機クリップ")]
    private string idleClip = "idle";
    // 移動時に再生する歩行アニメーションのクリップ名
    [SerializeField]
    [Label("歩行クリップ")]
    private string walkClip = "walk";

    // 現在HP、実行時のみ保持しシリアライズしない
    private int currentHP;
    // 被弾無敵の残り秒数、0より大きい間は被弾しない
    private float invincibleTimer;
    // 移動入力があるか、Moveで更新しアニメーション切り替えに使う
    private bool isMoving;
    // 現在再生中のクリップ名、変化時だけ切り替えて再生をリセットしない
    private string currentClip = "";
    // HP表示Entityのキャッシュ、EntityRef探索を毎回行わない
    private Entity cachedHpText;
    // 死亡時イベント、別スクリプトから購読できる
    public readonly GameEvent OnPlayerDied = new();

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

        // HPを初期化して表示へ反映する
        currentHP = maxHP;
        UpdateHpText();

        // Listの動作確認、各要素と合計をログ出力する
        int sum = 0;
        foreach (int number in testNumbers)
        {
            sum += number;
            Debug.Log($"testNumbers要素: {number}");
        }
        Debug.Log($"testNumbers件数={testNumbers.Count} 合計={sum}");

        // Listのランタイム挙動確認、後ろから1秒おきに削除し、空になったら復元する動作を繰り返す
        StartCoroutine(RunListDemo());

        // スクリプト間参照の動作確認、参照先のメソッドを呼んで現在値を読む
        if (TryResolveScoreKeeper(out ScoreKeeper keeper))
        {
            keeper.AddScore(0);
            Debug.Log($"scoreKeeper参照成功 現在スコア={keeper.CurrentScore}");
        }
        else
        {
            Debug.Log("scoreKeeper未設定または解決できません");
        }

        // コンポーネント参照の動作確認、解決したTextRendererの現在値を読む
        if (hpTextComponent.TryResolve(out TextRenderer hpComponent))
        {
            Debug.Log($"hpTextComponent参照成功 text=[{hpComponent.Text}]");
        }
        else
        {
            Debug.Log("hpTextComponent未設定または解決できません");
        }
    }
    //========================================================================
    //	毎フレーム更新処理
    //========================================================================
    public override void Update()
    {
        // 被弾無敵の残り時間を減らす
        if (invincibleTimer > 0.0f)
        {
            invincibleTimer -= Time.deltaTime;
        }
        // プレイヤーの移動
        Move();
        // 移動状態に応じて歩行と待機のアニメーションを切り替える
        UpdateAnimation();
        // ジャンプ入力の取得
        ReadJumpInput();
        // 9キーでプレファブを生成する
        if (Input.GetKeyDown(KeyCode.Alpha9))
        {
            SpawnPrefab();
        }
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
        // 衝突相手のエンティティをログ出力する
        Debug.Log($"OnCollisionEnter 相手={collision.entity.name} タグ={collision.entity.tag}");

        // Enemyタグに当たったらダメージを受ける
        if (collision.entity.CompareTag("Enemy"))
        {
            TakeDamage(1);
        }
        UpdateGrounded(collision);
    }
    public override void OnCollisionStay(Collision collision)
    {
        UpdateGrounded(collision);
    }
    public override void OnCollisionExit(Collision collision)
    {
        // 衝突相手のエンティティをログ出力する
        Debug.Log($"OnCollisionExit 相手={collision.entity.name} タグ={collision.entity.tag}");
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
        // 入力が無ければ停止扱いにして処理しない
        if (inputLength <= 0.0001f)
        {
            isMoving = false;
            return;
        }
        isMoving = true;

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
    //	移動状態に応じたアニメーション切り替え
    //========================================================================
    private void UpdateAnimation()
    {
        // SkinnedAnimationが無ければ何もしない
        if (!TryGet<SkinnedAnimation>(out SkinnedAnimation anim))
        {
            return;
        }
        // 移動中は歩行、停止中は待機を選ぶ
        string wantClip = isMoving ? walkClip : idleClip;
        // 同じクリップを毎フレーム設定すると再生がリセットされるため変化時のみ切り替える
        if (wantClip != currentClip)
        {
            currentClip = wantClip;
            anim.Clip = wantClip;
        }
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

    //========================================================================
    //	被弾処理、HPを減らし表示更新と死亡判定を行う
    //========================================================================
    private void TakeDamage(int amount)
    {
        // 無敵中は再接触の被弾を無視する、敵から離脱再接触したときの連続被弾を防ぐ
        if (invincibleTimer > 0.0f)
        {
            return;
        }
        invincibleTimer = invincibleTime;

        currentHP -= amount;
        if (currentHP < 0)
        {
            currentHP = 0;
        }
        Debug.Log($"被弾 HP={currentHP}/{maxHP}");
        UpdateHpText();

        // 被弾のたびに参照先スクリプトでスコアを加算する
        if (TryResolveScoreKeeper(out ScoreKeeper keeper))
        {
            keeper.AddScore(10);
        }

        // HPが尽きたら死亡イベントを発火しゲームオーバーへ移る
        if (currentHP <= 0)
        {
            OnPlayerDied.Invoke();
            GoToGameOver();
        }
    }
    //========================================================================
    //	Listのランタイム挙動確認、削除と復元を1秒間隔で繰り返す
    //========================================================================
    private IEnumerator RunListDemo()
    {
        // 復元時に同じ並びへ戻すため初期の中身を退避しておく
        List<int> original = new(testNumbers);

        while (true)
        {
            // 要素がある限り後ろから1秒おきに取り除く
            while (testNumbers.Count > 0)
            {
                yield return new WaitForSeconds(1.0f);
                int last = testNumbers[testNumbers.Count - 1];
                testNumbers.RemoveAt(testNumbers.Count - 1);
                Debug.Log($"List削除 値={last} 残り={testNumbers.Count}");
            }

            // 空になったら退避した並びへ1秒おきに戻していく
            foreach (int number in original)
            {
                yield return new WaitForSeconds(1.0f);
                testNumbers.Add(number);
                Debug.Log($"List復元 値={number} 件数={testNumbers.Count}");
            }
        }
    }
    //========================================================================
    //	HP表示の更新、現在HP / 最大HP をTextRendererへ書き込む
    //========================================================================
    private void UpdateHpText()
    {
        Entity textEntity = GetHpText();
        if (textEntity.isAlive && textEntity.TryGet(out TextRenderer text))
        {
            text.Text = $"{currentHP} / {maxHP}";
        }
    }
    //========================================================================
    //	HP表示EntityのEntityRef解決
    //========================================================================
    private Entity GetHpText()
    {
        if (!cachedHpText.isAlive)
        {
            cachedHpText = hpTextEntity.Resolve();
        }
        return cachedHpText;
    }
    //========================================================================
    //	ゲームオーバーシーンへの遷移
    //========================================================================
    private void GoToGameOver()
    {
        // シーン未設定なら遷移しない
        if (gameOverScene.IsNull)
        {
            Debug.LogWarning("gameOverScene未設定のため遷移できません");
            return;
        }
        SceneManager.LoadScene(gameOverScene, LoadSceneMode.Single);
    }
    //========================================================================
    //	プレファブ生成、自分の位置を基準に生成する
    //========================================================================
    private void SpawnPrefab()
    {
        // プレファブ未設定なら生成しない
        if (spawnPrefab.IsNull)
        {
            Debug.LogWarning("spawnPrefab未設定のため生成できません");
            return;
        }
        Vector3 spawnPosition = transform.position + Vector3.up * spawnHeightOffset;
        Entity spawned = Prefab.Instantiate(spawnPrefab, spawnPosition, transform.rotation);
        Debug.Log($"プレファブ生成: {spawned.name}");
    }
    //========================================================================
    //	参照先ScoreKeeperの解決
    //========================================================================
    private bool TryResolveScoreKeeper(out ScoreKeeper keeper)
    {
        keeper = null!;
        if (!scoreKeeper.isValid)
        {
            return false;
        }
        Entity keeperEntity = scoreKeeper.entity.Resolve();
        if (!keeperEntity.isAlive)
        {
            return false;
        }
        keeper = keeperEntity.GetComponent<ScoreKeeper>()!;
        return keeper != null;
    }
}