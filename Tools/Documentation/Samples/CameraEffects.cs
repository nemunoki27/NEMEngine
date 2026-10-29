using NEMEngine;

public sealed class CameraEffects : MonoBehaviour {

    void Start() {
        // 同じCameraへ設定されたBloom Passを探す
        var camera = GetComponent<PerspectiveCamera>();
        if (camera == null) return;
        var bloom = camera.renderPasses.FindPass("Bloom");
        if (!bloom.isValid) return;
        // このCameraの実行中の値へ反映
        bloom.SetEnabled(true);
        bloom.SetFloat("intensity", 1.5f);
    }
}
