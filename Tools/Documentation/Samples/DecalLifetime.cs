using NEMEngine;

public sealed class DecalLifetime : MonoBehaviour {

    [SerializeField, Min(0.01f)] private float lifetime = 5.0f;
    [SerializeField, Min(0.01f)] private float fadeDuration = 1.0f;
    private DecalProjector? decal;
    private float elapsed;

    void Start() {
        // 投影設定を持つDecalProjectorを保持
        decal = GetComponent<DecalProjector>();
    }

    void Update() {
        // 寿命の終わりだけAlphaを下げる
        if (decal == null) return;
        elapsed += Time.deltaTime;
        float fade = MathF.Max(0.01f, MathF.Min(fadeDuration, lifetime));
        float progress = (elapsed - (lifetime - fade)) / fade;
        decal.fadeFactor = Mathf.Clamp(1.0f - progress, 0.0f, 1.0f);

        // 透明になった後に投影用のGameObjectを破棄
        if (elapsed >= lifetime) NEMEngine.Object.Destroy(gameObject);
    }
}
