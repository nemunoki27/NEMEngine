using NEMEngine;

public sealed class MotionDash : MonoBehaviour {

    private MotionAnimation? motion;

    void Start() {
        // Inspectorで開始姿勢からの移動を選び、前方へ短く踏み込む設定にする
        motion = GetComponent<MotionAnimation>();
        if (motion == null) return;
        var settings = motion.GetSettings();
        settings.PlayOnStart = false;
        settings.Offset = new Vector3(0, 0, 3);
        settings.Duration = 0.25f;
        motion.SetSettings(settings);
    }

    void Update() {
        // 入力時の姿勢を原点として踏み込みを開始する
        if (motion != null && Input.GetKeyDown(KeyCode.E)) {
            motion.Play();
        }
    }
}
