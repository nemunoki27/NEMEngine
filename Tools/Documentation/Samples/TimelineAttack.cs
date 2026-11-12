using NEMEngine;

public sealed class TimelineAttack : MonoBehaviour {

    private TimelinePlayer? player;

    void Start() {
        // Inspectorで攻撃用タイムラインと割り当て先を設定しておく
        player = GetComponent<TimelinePlayer>();
        if (player != null) player.PlayOnStart = false;
    }

    void Update() {
        // 攻撃中の連打では再開せず、停止中だけ先頭から攻撃する
        if (player != null && !player.IsPlaying && Input.GetKeyDown(KeyCode.E)) player.Play();
    }
}
