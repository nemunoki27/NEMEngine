using NEMEngine;

public sealed class LocalSlowMotion : MonoBehaviour {

    private TimeScale? scale;
    private bool slow;

    void Start() {
        // 子のエフェクトも含め、時間源を自身・親階層に設定しておく
        scale = GetComponent<TimeScale>();
        if (scale != null) scale.ApplyToChildren = true;
    }

    void Update() {
        // Eキーで、このGameObjectと子の処理だけを通常速度と20%速度に切り替える
        if (scale != null && Input.GetKeyDown(KeyCode.E)) {
            slow = !slow;
            scale.Scale = slow ? 0.2f : 1.0f;
        }
    }
}
