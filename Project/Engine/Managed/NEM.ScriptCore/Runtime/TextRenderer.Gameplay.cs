namespace NEMEngine;

// 自動生成される TextRenderer wrapper の gameplay method 拡張（生成ファイルは編集しない）。
// マテリアルの color を C# から get/set する。パラメータ名は color→baseColor→albedo の順で自動解決する。
public sealed partial class TextRenderer {

    public DynamicBuffer<TextCharacterTransform> CharacterTransforms =>
        gameObject.GetBuffer<TextCharacterTransform>();

    public MaterialInstance MaterialInstance =>
        new(gameObject, RendererMaterialTarget.Text);

    // マテリアル color を上書きする。
    public void SetColor(Color4 color) {
        MaterialInstance.SetColor(MaterialParameterIDs.BaseColor, MaterialParameterNames.BaseColor, color);
    }
    public void SetColor(Color3 color) {
        SetColor(new Color4(color.r, color.g, color.b, 1.0f));
    }

    // 現在のマテリアル color を取得する。
    public Color4 GetColor() {
        return MaterialInstance.TryGetColor(MaterialParameterNames.BaseColor, out Color4 color)
            ? color
            : new Color4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}
