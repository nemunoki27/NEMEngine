using NEMEngine;

public sealed class MaterialTint : MonoBehaviour {

    void Start() {
        // 色を変えるRendererを取得
        var renderer = GetComponent<PrimitiveRenderer>();
        if (renderer == null) return;
        // 共有Assetを変えず、この物体だけ上書き
        bool applied = renderer.MaterialInstance.SetColor(
            MaterialParameterNames.BaseColor, new Color4(0.2f, 0.8f, 1.0f, 1.0f));
        // Materialが指定Parameterを持つか確認
        if (!applied) Debug.LogWarning("ベースカラーの設定を確認してください");
    }
}
