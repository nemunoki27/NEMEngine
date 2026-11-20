using NEMEngine;

public sealed class BeamAttack : MonoBehaviour {

    [SerializeField] private GameObject? impact;
    [SerializeField] private Texture? beamTexture;
    private BeamRenderer? beam;

    void Start() {
        // 銃口に付けたビームを取得する
        beam = GetComponent<BeamRenderer>();
        if (beam == null) return;

        // 銃口から先端へ色を変え、このビームだけを発光させる
        var material = beam.MaterialInstance;
        material.SetColor("startColor", Color4.white);
        material.SetColor("targetColor", Color4.blue);
        material.SetColor("emissiveColor", Color4.blue);
        material.SetFloat("emissiveIntensity", 2.0f);
        material.SetTexture("baseColorTexture", beamTexture);
    }

    void Update() {
        if (beam == null) return;

        // ボタンを押している間だけ照射する
        if (Input.GetMouseButtonDown(0)) beam.Play();
        if (Input.GetMouseButtonUp(0)) beam.Stop();

        // 着弾エフェクトを命中地点へ移動する
        if (impact == null) return;
        bool hasHit = beam.TryGetHit(out RaycastHit hit, out _);
        impact.SetActive(hasHit);
        if (hasHit) impact.transform.position = hit.point;
    }
}
