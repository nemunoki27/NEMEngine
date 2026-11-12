using NEMEngine;

public sealed class DayNightPreview : MonoBehaviour {

    private DayNightCycle? cycle;

    void Start() {
        // 太陽・月・空はInspectorで割り当て、朝6時から2分で1日を進める
        cycle = GetComponent<DayNightCycle>();
        if (cycle == null) return;
        cycle.StartHour = 6.0f;
        cycle.DayDurationSeconds = 120.0f;
        cycle.FixedTime = false;
        cycle.Play();
    }

    void Update() {
        // Eキーで夜22時へ移動し、月明かりを確認する
        if (cycle != null && Input.GetKeyDown(KeyCode.E)) cycle.SetTime(22.0f);
    }
}
