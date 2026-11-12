using NEMEngine;

namespace NEM.ScriptAnalyzers.Tests.Samples;

// ゲーム側で対象と入力を判断し演出の時刻はTimelineへ任せる
internal sealed class TimelineAttackExample : MonoBehaviour {
    [SerializeField] private TimelineAsset? attack = null;
    private TimelinePlayer? player;

    private void Awake() {
        // 同じTimelineアセットを別のキャラクターでも使用できる
        player = GetComponent<TimelinePlayer>();
        if (player != null && attack != null) player.Timeline = attack;
    }

    public void BeginAttack(GameObject? target) {
        if (player == null) return;
        // Targetの距離による接近と通常移動の分岐はツール側で設定する
        player.SetBinding("Target", target);
        player.Play();
    }

    public bool TryContinueCombo() {
        // 実際の入力判定と次の技の選択はゲーム固有の処理で行う
        return player != null && player.IsWindowOpen("Combo");
    }

    public void CancelAttack() {
        // 判定や生成物の終了処理はTimelineの停止処理へ集約する
        player?.Stop();
    }
}

[TimelineClipType("nem.examples.timeline_offset", "相対位置の補間", SupportsPreview = true)]
internal sealed class TimelineOffsetExample : TimelineClip {
    [SerializeField] private Vector3 offset = new(0, 0.5f, 0);
    private GameObject? actor;
    private Vector3 origin;

    public override void OnEnter(TimelineContext context) {
        // プレビュー用の割り当ても実行時の割り当ても同じ入口で取得する
        actor = context.GetBinding("Actor");
        if (actor != null) origin = actor.transform.localPosition;
    }

    public override void Evaluate(TimelineContext context) {
        if (actor == null) return;
        // 差分加算ではなく絶対時刻で評価しシーク時にも同じ結果を得る
        float progress = context.Duration > 0 ? (float)System.Math.Clamp(context.LocalTime / context.Duration, 0, 1) : 1;
        actor.transform.localPosition = origin + offset * progress;
    }

    public override void OnExit(TimelineContext context, TimelineExitReason reason) {
        // 独自クリップが変更した値も編集プレビューの終了時には復元する
        if (context.IsPreview && actor != null) actor.transform.localPosition = origin;
        actor = null;
    }
}
