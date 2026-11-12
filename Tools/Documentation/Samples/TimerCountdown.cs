using NEMEngine;

public sealed class TimerCountdown : MonoBehaviour {

    private NEMEngine.Timer? timer;
    private bool notified;

    void Start() {
        // Inspectorの計測方式を秒数にし、3秒のカウントダウンにする
        timer = GetComponent<NEMEngine.Timer>();
        if (timer == null) return;
        timer.PlayOnStart = false;
        timer.TargetSeconds = 3.0f;
    }

    void Update() {
        // Eキーで先頭から計測し直す
        if (timer == null) return;
        if (Input.GetKeyDown(KeyCode.E)) {
            notified = false;
            timer.Play();
        }
        // 到達後に毎フレーム通知しないよう、処理済み状態を保持する
        if (timer.IsReached && !notified) {
            notified = true;
            Debug.Log("カウントダウン完了");
        }
    }
}
