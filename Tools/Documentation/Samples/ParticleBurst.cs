using NEMEngine;

public sealed class ParticleBurst : MonoBehaviour {

    private ParticleSystem? system;

    void Start() {
        // 自動再生の設定と、現在の再生状態を別々に変更
        system = GetComponent<ParticleSystem>();
        if (system == null) return;
        system.PlayOnAwake = false;
        system.Stop(stopBehavior: ParticleSystemStopBehavior.StopEmittingAndClear);
    }

    void Update() {
        // Eキーを押したframeだけ単発再生
        if (system != null && Input.GetKeyDown(KeyCode.E)) system.PlayOneShot();
    }
}
