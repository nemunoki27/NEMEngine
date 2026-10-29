using NEMEngine;

public sealed class PulsingLight : MonoBehaviour {

    [SerializeField, Min(0.0f)] private float baseIntensity = 10.0f;
    [SerializeField, Min(0.0f)] private float speed = 2.0f;
    private PointLight? light;

    void Start() {
        // 位置や照射範囲はEditorの設定を使う
        light = GetComponent<PointLight>();
    }

    void Update() {
        // 強度を滑らかに変化させてランプの明滅を作る
        if (light != null) light.Intensity = baseIntensity * (1.0f + 0.25f * MathF.Sin(Time.time * speed));
    }
}
