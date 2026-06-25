using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	ScoreKeeper
//	スクリプト間参照とイベント購読の動作確認用
//============================================================================
public sealed class ScoreKeeper : ScriptBehaviour
{
    // 現在スコア
    [SeparatorText("スコア")]
    [SerializeField]
    [Label("現在スコア")]
    [Tooltip("被弾するたびに加算される現在スコア")]
    private int score;

    // 死亡イベントを購読する相手、スクリプト間参照とイベント連携の確認に使う
    [SeparatorText("参照")]
    [SerializeField]
    [Label("プレイヤー")]
    [Tooltip("OnPlayerDiedを購読するPlayerActionへの参照")]
    private ScriptRef<PlayerAction> player;

    //========================================================================
    //	開始処理、参照先プレイヤーの死亡イベントを購読する
    //========================================================================
    public override void Start()
    {
        // 参照が未設定なら購読しない
        if (!player.isValid)
        {
            Debug.Log("ScoreKeeper: player未設定のため購読しません");
            return;
        }

        // EntityRefからPlayerActionを解決し、owner破棄で自動解除される購読を張る
        Entity playerEntity = player.entity.Resolve();
        if (playerEntity.isAlive && playerEntity.GetComponent<PlayerAction>() is PlayerAction action)
        {
            action.OnPlayerDied.Subscribe(this, HandlePlayerDied);
            Debug.Log("ScoreKeeper: PlayerActionのOnPlayerDiedを購読しました");
        }
    }

    //========================================================================
    //	スコア加算、PlayerActionから呼ばれてスクリプト間参照を確認する
    //========================================================================
    public void AddScore(int amount)
    {
        score += amount;
        Debug.Log($"ScoreKeeper: スコア加算 +{amount} 合計={score}");
    }

    // 現在スコアの取得
    public int CurrentScore => score;

    //========================================================================
    //	プレイヤー死亡イベントのハンドラ
    //========================================================================
    private void HandlePlayerDied()
    {
        Debug.Log($"ScoreKeeper: プレイヤー死亡を受信 最終スコア={score}");
    }
}
