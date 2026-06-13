namespace NEMEngine;

// EntityRef が指す対象の種別。
public enum EntityRefKind {

    Null = 0,
    // scene asset 内の Entity（sourceAsset=scene, localFileId=scene-local stable ID）
    Scene,
    // prefab asset 内の Entity（sourceAsset=prefab, localFileId=prefab-local stable ID）
    Prefab,
}

// scene / prefab 内 Entity への authoring 参照。
// runtime entity index / generation / ECSWorld* は保存せず、永続 identity だけを保持する。
// runtime resolve 失敗時も serialized identity を保持し、Missing Entity でも値を破壊しない。
// runtime での解決（scene instance / prefab instance との組み合わせ）は runtime 側へ委ね、
// ここでは serialization contract と identity の保持だけを担う。
public readonly struct EntityRef {

    // 参照の種別
    public readonly EntityRefKind kind;
    // 参照元の scene / prefab asset の UUID
    public readonly UUID sourceAsset;
    // scene / prefab ファイル内での安定 ID（SceneObjectComponent.localFileID）
    public readonly UUID localFileId;

    public EntityRef(EntityRefKind kind, UUID sourceAsset, UUID localFileId) {
        this.kind = kind;
        this.sourceAsset = sourceAsset;
        this.localFileId = localFileId;
    }

    public bool isValid => kind != EntityRefKind.Null && localFileId.isValid;

    public static EntityRef Null => new(EntityRefKind.Null, UUID.None, UUID.None);

    public static EntityRef Scene(UUID sceneAsset, UUID localFileId)
        => new(EntityRefKind.Scene, sceneAsset, localFileId);

    public static EntityRef Prefab(UUID prefabAsset, UUID localFileId)
        => new(EntityRefKind.Prefab, prefabAsset, localFileId);
}
