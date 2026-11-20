namespace NEMEngine;

// 共有Materialへ書き込まず投影ごとの値を変更する
public sealed partial class DecalProjector {

    public Color4 color {
        get => MaterialInstance.TryGetColor("color", out var value) ? value : Color4.white;
        set => MaterialInstance.SetColor("color", value);
    }

    public float fadeFactor {
        get => MaterialInstance.TryGetFloat("opacity", out var value) ? value : 1.0f;
        set => MaterialInstance.SetFloat("opacity", value);
    }

    public Vector2 uvScale {
        get => GetComponent<UVTransform>()?.Scale ?? Vector2.one;
        set => GetOrAddUV().Scale = value;
    }

    public Vector2 uvBias {
        get => GetComponent<UVTransform>()?.Position ?? Vector2.zero;
        set => GetOrAddUV().Position = value;
    }

    public float uvRotation {
        get => GetComponent<UVTransform>()?.Rotation ?? 0.0f;
        set => GetOrAddUV().Rotation = value;
    }

    private UVTransform GetOrAddUV() => GetComponent<UVTransform>() ?? AddComponent<UVTransform>();

    public MaterialInstance MaterialInstance => new(gameObject, RendererMaterialTarget.Decal);
}
