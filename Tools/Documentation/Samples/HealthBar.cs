using NEMEngine;

public sealed class HealthBar : MonoBehaviour {

    [SerializeField, Min(1.0f)] private float maxHP = 100.0f;
    private UIProgress? bar;
    private float hp;

    void Start() {
        // HPの範囲と、通常・遅延表示の補間を用意
        bar = GetComponent<UIProgress>();
        hp = maxHP;
        if (bar == null) return;
        bar.MinValue = 0.0f;
        bar.MaxValue = maxHP;
        bar.Smooth = true;
        bar.SmoothDuration = 0.15f;
        bar.Delayed = true;
        bar.DelayedWait = 0.25f;
        bar.DelayedDuration = 0.4f;
        bar.Value = hp;
    }

    public void ApplyDamage(float amount) {
        // 被弾時の値を範囲内に収め、バーへ反映
        hp = Mathf.Clamp(hp - MathF.Max(0.0f, amount), 0.0f, maxHP);
        if (bar != null) bar.Value = hp;
    }
}
