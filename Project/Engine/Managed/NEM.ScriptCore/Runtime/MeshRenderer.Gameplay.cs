namespace NEMEngine;

// 自動生成される MeshRenderer wrapper の gameplay method 拡張（生成ファイルは編集しない）。
// マテリアルの color を C# から get/set する。パラメータ名は color→baseColor→albedo の順で自動解決する。
public sealed partial class MeshRenderer {

    // subMeshIndex -1は全サブメッシュへ同じ値を設定する
    public MaterialInstance MaterialInstance =>
        new(entity, RendererMaterialTarget.Mesh);

    public MaterialInstance GetMaterialInstance(int subMeshIndex) =>
        new(entity, RendererMaterialTarget.Mesh, subMeshIndex);

    // 全サブメッシュのマテリアル color を上書きする。
    public void SetColor(Color4 color) {
        MaterialInstance.SetColor(MaterialParameterIDs.BaseColor, MaterialParameterNames.BaseColor, color);
    }
    public void SetColor(Color3 color) {
        SetColor(new Color4(color.r, color.g, color.b, 1.0f));
    }

    // 指定サブメッシュのマテリアル color を上書きする。
    public void SetColor(int subMeshIndex, Color4 color) {
        GetMaterialInstance(subMeshIndex).SetColor(
            MaterialParameterIDs.BaseColor, MaterialParameterNames.BaseColor, color);
    }
    public void SetColor(int subMeshIndex, Color3 color) {
        SetColor(subMeshIndex, new Color4(color.r, color.g, color.b, 1.0f));
    }

    // 現在のマテリアル color を取得する。先頭サブメッシュを代表値として返す。
    public Color4 GetColor() {
        return MaterialInstance.TryGetColor(MaterialParameterNames.BaseColor, out Color4 color)
            ? color
            : new Color4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    public Color4 GetColor(int subMeshIndex) {
        return GetMaterialInstance(subMeshIndex).TryGetColor(
            MaterialParameterNames.BaseColor, out Color4 color)
            ? color
            : new Color4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}
