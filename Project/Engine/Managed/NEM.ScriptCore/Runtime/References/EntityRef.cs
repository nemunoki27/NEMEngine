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
    // 参照元のシーン、プレファブアセットUUID
    public readonly UUID sourceAsset;
    // シーン、プレファブファイル内での安定ID
    public readonly UUID localFileId;

    public EntityRef(EntityRefKind kind, UUID sourceAsset, UUID localFileId)
    {
        this.kind = kind;
        this.sourceAsset = sourceAsset;
        this.localFileId = localFileId;
    }

    public bool isValid => kind != EntityRefKind.Null && localFileId.isValid;

    // 参照先の現在のランタイムワールドエンティティを取得する
    public Entity Resolve() => isValid ? NativeApi.ResolveEntityReference(sourceAsset.value, localFileId.value) : Entity.nullEntity;

    public static EntityRef Null => new(EntityRefKind.Null, UUID.None, UUID.None);

    public static EntityRef Scene(UUID sceneAsset, UUID localFileId) => new(EntityRefKind.Scene, sceneAsset, localFileId);

    public static EntityRef Prefab(UUID prefabAsset, UUID localFileId) => new(EntityRefKind.Prefab, prefabAsset, localFileId);
}
