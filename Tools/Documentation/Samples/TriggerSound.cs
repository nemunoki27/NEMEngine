using NEMEngine;

public sealed class TriggerSound : MonoBehaviour {

    private AudioSource? source;

    void Start() {
        // 自動再生を止め、侵入時だけ音を鳴らす
        source = GetComponent<AudioSource>();
        if (source == null) return;
        source.PlayOnAwake = false;
        source.Loop = false;
        source.Stop();
    }

    void OnCollisionEnter(Collision collision) {
        // Playerが範囲へ入ったときだけ効果音を重ねて再生
        if (source == null || collision.gameObject == null || !collision.gameObject.CompareTag("Player")) return;
        AudioClip? clip = source.Clip;
        if (clip != null) source.PlayOneShot(clip);
    }
}
