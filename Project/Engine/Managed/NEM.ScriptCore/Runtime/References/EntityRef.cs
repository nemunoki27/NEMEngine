namespace NEMEngine;

// EntityRef が指す対象の種別
internal enum EntityRefKind
{

    Null = 0,
    Scene,
    Prefab,
}

// Entity参照の保存identity。ゲームコードへは公開せず、Entity型フィールドのシリアライズ内部表現として使う
internal readonly struct EntityRef
{

    // 参照の種別
    public readonly EntityRefKind kind;
    // 参照元のシーン、プレファブアセットGUID
    public readonly AssetGUID sourceAsset;
    // シーン、プレファブファイル内での安定ID
    public readonly UUID localFileId;

    public EntityRef(EntityRefKind kind, AssetGUID sourceAsset, UUID localFileId)
    {
        this.kind = kind;
        this.sourceAsset = sourceAsset;
        this.localFileId = localFileId;
    }

    public bool isValid => kind != EntityRefKind.Null && localFileId.isValid;

    // 参照先の現在のランタイムワールドエンティティを取得する
    public Entity Resolve() => isValid ? NativeApi.ResolveEntityReference(sourceAsset, localFileId.value) : Entity.nullEntity;

    public static EntityRef Null => new(EntityRefKind.Null, AssetGUID.None, UUID.None);

    public static EntityRef Scene(AssetGUID sceneAsset, UUID localFileId) => new(EntityRefKind.Scene, sceneAsset, localFileId);

    public static EntityRef Prefab(AssetGUID prefabAsset, UUID localFileId) => new(EntityRefKind.Prefab, prefabAsset, localFileId);
}
