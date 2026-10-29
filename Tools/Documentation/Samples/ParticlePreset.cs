using NEMEngine;

public sealed class ParticlePreset : MonoBehaviour {

    void Start() {
        // 現在のParticle設定のSnapshotを取得
        var particle = GetComponent<ParticleSystem>();
        if (particle == null) return;
        var settings = particle.GetSettings();
        // 発生数の上限と発光Moduleを調整
        settings.emitter.maxParticles = 1000;
        var phase = settings.phases.Count > 0 ? settings.phases[0] : settings.AddPhase("発光");
        var glow = phase.GetEmissive() ?? phase.AddEmissive();
        glow.useParticleColor = true;
        glow.startIntensity = 3.0f;
        glow.endIntensity = 0.0f;
        // 変更した設定を戻してから再生
        particle.SetSettings(settings);
        particle.Play();
    }
}
