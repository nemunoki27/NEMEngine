using System.Collections;
using System.Collections.Generic;
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

    // 順番に入れ替えるテクスチャのリスト、List<AssetRef>の動作確認も兼ねる
    [SeparatorText("テクスチャ切替")]
    [SerializeField]
    [Label("切替テクスチャ")]
    [Tooltip("SpriteRendererへ順に設定していくテクスチャの一覧")]
    private List<AssetRef<TextureAsset>> cycleTextures = new();
    // 切り替え間隔(秒)
    [SerializeField]
    [Label("切替間隔")]
    private float cycleInterval = 1.0f;

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

        // テクスチャの順次入れ替えをRunListDemoと同じくコルーチンで回す
        StartCoroutine(CycleTextures());
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
        if (Input.GetKey(KeyCode.W)) { input.y += 1.0f; }
        if (Input.GetKey(KeyCode.S)) { input.y -= 1.0f; }
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
        Vector3 moveDirection = new Vector3(input.x, 0.0f, input.y);
        Vector3 direction = Vector3.Normalize(moveDirection);

        // 入力の強さに応じて速度を変えて移動する
        transform.position += direction * (moveSpeed * inputLength * Time.deltaTime);
    }
    //========================================================================
    //	テクスチャの順次入れ替え、リストを巡回しSpriteRendererへ設定する
    //========================================================================
    private IEnumerator CycleTextures()
    {
        // テクスチャが無ければ巡回しない
        if (cycleTextures.Count == 0)
        {
            yield break;
        }

        int index = 0;
        while (true)
        {
            // 自分のSpriteRendererのテクスチャを現在のものへ入れ替える
            if (TryGet<SpriteRenderer>(out SpriteRenderer sprite))
            {
                sprite.Texture = cycleTextures[index];
                Debug.Log($"SpriteTexture切替 index={index}/{cycleTextures.Count}");
            }
            // 次のテクスチャへ進め、末尾まで行ったら先頭へ戻る
            index = (index + 1) % cycleTextures.Count;
            yield return new WaitForSeconds(cycleInterval);
        }
    }
}
