using NEMEngine;

public sealed class SwordTrail : MonoBehaviour {

    private TrailRenderer? trail;

    void Start() {
        // 攻撃中だけ剣の軌跡を生成する
        trail = GetComponent<TrailRenderer>();
        if (trail != null) trail.Emitting = false;
    }

    void Update() {
        if (trail == null) return;
        // ボタンを離すと記録を止め、残った軌跡を寿命まで表示する
        if (Input.GetMouseButtonDown(0)) trail.Emitting = true;
        if (Input.GetMouseButtonUp(0)) trail.Emitting = false;
    }
}
