using NEMEngine;

public sealed class AtmosphereFog : MonoBehaviour {

    private ExponentialHeightFog? fog;
    private bool dense;

    void Start() {
        // 同じGameObjectへ空と霧を追加し、空の太陽はInspectorで設定しておく
        var sky = GetComponent<SkyAtmosphere>();
        if (sky != null) sky.IblIntensity = 1.0f;
        fog = GetComponent<ExponentialHeightFog>();
        if (fog == null) return;
        fog.Density = 0.01f;
        fog.StartDistance = 5.0f;
    }

    void Update() {
        // Eキーで霧を濃くし、遠景の見え方を切り替える
        if (fog != null && Input.GetKeyDown(KeyCode.E)) {
            dense = !dense;
            fog.Density = dense ? 0.04f : 0.01f;
        }
    }
}
