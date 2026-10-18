namespace NEMEngine;

// 共有Materialへ書き込まず投影ごとの値を変更する
public sealed partial class DecalProjector {

    public MaterialInstance MaterialInstance => new(gameObject, RendererMaterialTarget.Decal);
}
