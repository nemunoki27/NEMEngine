namespace NEMEngine;

// EntityRef が指す対象の種別
internal enum EntityRefKind
{

    Null = 0,
    Scene,
    Prefab,
}

// GameObject参照の保存identity。ゲームコードへは公開せず、GameObject型フィールドのシリアライズ内部表現として使う
internal readonly struct EntityRef
{

    // 参照の種別
    public readonly EntityRefKind kind;
    // 参照元のシーン、プレファブアセットGUID
    public readonly AssetGUID sourceAsset;
    // シーン、プレファブファイル内での安定ID
    public readonly UUID localFileID;

    public EntityRef(EntityRefKind kind, AssetGUID sourceAsset, UUID localFileID)
    {
        this.kind = kind;
        this.sourceAsset = sourceAsset;
        this.localFileID = localFileID;
    }

    public bool isValid => kind != EntityRefKind.Null && localFileID.isValid;

    // 参照先の現在のランタイムワールドエンティティを取得する
    public GameObject? Resolve() => isValid ? NativeEntityAPI.ResolveEntityReference(sourceAsset, localFileID.value) : null;

    public static EntityRef Null => new(EntityRefKind.Null, AssetGUID.None, UUID.None);

    public static EntityRef Scene(AssetGUID sceneAsset, UUID localFileID) => new(EntityRefKind.Scene, sceneAsset, localFileID);

    public static EntityRef Prefab(AssetGUID prefabAsset, UUID localFileID) => new(EntityRefKind.Prefab, prefabAsset, localFileID);
}
