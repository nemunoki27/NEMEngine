namespace NEMEngine;

// asset 種別マーカー。AssetRef<TAsset> の typed filter を表すための最小限の型。
// Inspector filter と serialization に必要な情報だけを持つ。
public interface IAssetType {
}

// マーカー型へ native AssetType 名（C++ Engine::AssetType の列挙名）を関連付ける。
// source generator がこの値を schema の reference filter として出力する。
[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct, AllowMultiple = false, Inherited = false)]
public sealed class NativeAssetTypeAttribute : Attribute {

    public NativeAssetTypeAttribute(string nativeType) {
        NativeType = nativeType;
    }

    public string NativeType { get; }
}

// 既存 Engine::AssetType に対応する組み込みマーカー。必要な型だけ最小限用意する。
[NativeAssetType("Texture")] public sealed class TextureAsset : IAssetType { }
[NativeAssetType("Material")] public sealed class MaterialAsset : IAssetType { }
[NativeAssetType("Mesh")] public sealed class MeshAsset : IAssetType { }
[NativeAssetType("Prefab")] public sealed class PrefabAsset : IAssetType { }
[NativeAssetType("Scene")] public sealed class SceneAsset : IAssetType { }
[NativeAssetType("Audio")] public sealed class AudioAsset : IAssetType { }
[NativeAssetType("Font")] public sealed class FontAsset : IAssetType { }
[NativeAssetType("AnimationClip")] public sealed class AnimationClipAsset : IAssetType { }

// typed asset 参照。runtime pointer や filesystem path ではなく asset UUID を保存する。
// 参照先が見つからなくても UUID は保持し、Missing Asset でも値を破壊しない。
public readonly struct AssetRef<TAsset> where TAsset : class, IAssetType {

    // 参照先 asset の UUID（64bit）。0 は未設定。
    public readonly UUID id;

    public AssetRef(UUID id) {
        this.id = id;
    }

    public bool isValid => id.isValid;

    // gameplay 向け PascalCase アクセサ。実体の存在確認は Assets.Exists<T>() で別途行う（hot path で path lookup しない）。
    public bool IsNull => !id.isValid;
    public bool IsValid => id.isValid;
    public UUID AssetId => id;

    public static AssetRef<TAsset> None => new(UUID.None);
}
