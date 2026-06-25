namespace NEMEngine;

// 自動生成される SpriteRenderer wrapper の gameplay method 拡張（生成ファイルは編集しない）。
// マテリアルの color を C# から get/set する。パラメータ名は color→baseColor→albedo の順で自動解決する。
public readonly partial struct SpriteRenderer {

    // マテリアル color を上書きする。
    public void SetColor(Color4 color) {
        NativeApi.WriteRendererMaterialColor(entity.native, 1, -1, "", color.r, color.g, color.b, color.a);
    }
    public void SetColor(Color3 color) {
        NativeApi.WriteRendererMaterialColor(entity.native, 1, -1, "", color.r, color.g, color.b, 1.0f);
    }

    // 現在のマテリアル color を取得する。
    public Color4 GetColor() {
        return NativeApi.ReadRendererMaterialColor(entity.native, 1, -1);
    }
}
