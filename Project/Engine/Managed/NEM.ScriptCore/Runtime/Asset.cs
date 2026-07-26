namespace NEMEngine;

// asset種別クラスへnative AssetType名(C++ Engine::AssetTypeの列挙名)を関連付ける。
// source generatorがこの値をschemaのreference filterとして出力する。
[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
public sealed class NativeAssetTypeAttribute : Attribute {

    public NativeAssetTypeAttribute(string nativeType) {
        NativeType = nativeType;
    }

    public string NativeType { get; }
}

// アセット参照の共通基底。AssetGUIDだけを持つ不変ハンドルで、native resource / GPU pointer / filesystem pathは保持しない。
// nullが未割り当てを表す(Unityのアセット参照と同じ扱い)。Missing Assetでも参照identityは保持される。
public abstract class Asset : Object {

    // 参照先assetのGUID
    internal readonly AssetGUID id;

    private protected Asset(AssetGUID id) {
        this.id = id;
    }

    public AssetGUID assetId => id;

    // assetが現在のAssetDatabaseに存在するか。hot pathで繰り返す場合は結果を呼び出し側でcacheする想定
    public bool exists => id.isValid && NativeApi.ReadAssetExists(id);

    // 表示名(拡張子なしファイル名)。Missing/未設定はnull。path文字列そのものは返さない
    public string? name {
        get {
            if (!id.isValid) {
                return null;
            }
            string displayName = NativeApi.ReadAssetDisplayName(id);
            return string.IsNullOrEmpty(displayName) ? null : displayName;
        }
    }

    internal override bool objectAlive => id.isValid;

    // 同型かつ同一UUIDなら同じassetとして扱う
    private protected override bool EqualsObject(Object other) =>
        other is Asset asset && asset.GetType() == GetType() && asset.id == id;

    public override int GetHashCode() => HashCode.Combine(GetType(), id);
}

// 組み込みアセット型。型宣言だけでserialized fieldのアセット参照として扱える
[NativeAssetType("Texture")]
public sealed class Texture : Asset {
    internal Texture(AssetGUID id) : base(id) { }
}

[NativeAssetType("Material")]
public sealed class Material : Asset {
    internal Material(AssetGUID id) : base(id) { }
}

[NativeAssetType("Mesh")]
public sealed class Mesh : Asset {
    internal Mesh(AssetGUID id) : base(id) { }
}

[NativeAssetType("Scene")]
public sealed class SceneAsset : Asset {
    internal SceneAsset(AssetGUID id) : base(id) { }
}

[NativeAssetType("Audio")]
public sealed class AudioClip : Asset {
    internal AudioClip(AssetGUID id) : base(id) { }
}

[NativeAssetType("Font")]
public sealed class Font : Asset {
    internal Font(AssetGUID id) : base(id) { }
}

[NativeAssetType("AnimationClip")]
public sealed class AnimationClip : Asset {
    internal AnimationClip(AssetGUID id) : base(id) { }
}

[NativeAssetType("ParticleEffect")]
public sealed class ParticleEffect : Asset {
    internal ParticleEffect(AssetGUID id) : base(id) { }
}
