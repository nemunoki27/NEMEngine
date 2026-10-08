namespace NEMEngine;

using static NEMEngine.NativeAPI;

// 接続済みcallbackを用途別に呼び出す
internal static unsafe class NativeEntityAPI {

    // Scriptの削除を予約する
    internal static void EnqueueRemoveScript(NativeEntity entity, ulong slotID) {

        if (RemoveScript != null && slotID != 0) {
            RemoveScript(entity, slotID);
        }
    }

    // Entityの生存状態を返す
    internal static bool ReadIsAlive(NativeEntity entity) {

        return IsAlive != null && IsAlive(entity) != 0;
    }

    // Entityの名前を読む
    internal static string ReadName(NativeEntity entity) {

        if (CopyName == null) {
            return string.Empty;
        }

        return ManagedUTF8Transfer.ReadEntityString(CopyName, entity);
    }

    // Entityの名前を設定する
    internal static void WriteName(NativeEntity entity, string value) {

        if (SetName == null) {
            return;
        }

        string safeValue = value ?? string.Empty;
        byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(safeValue);
        fixed (byte* ptr = bytes) {
            SetName(entity, ptr);
        }
    }

    // Entity自身の有効状態を返す
    internal static bool ReadActiveSelf(NativeEntity entity) {

        return GetActiveSelf == null || GetActiveSelf(entity) != 0;
    }

    // Entity自身の有効状態を設定する
    internal static void WriteActiveSelf(NativeEntity entity, bool value) {

        if (SetActiveSelf != null) {
            SetActiveSelf(entity, value ? 1 : 0);
        }
    }

    // 階層を含めた有効状態を返す
    internal static bool ReadActiveInHierarchy(NativeEntity entity) {

        return GetActiveInHierarchy != null && GetActiveInHierarchy(entity) != 0;
    }

    // Componentの有無を返す
    internal static bool ReadHasComponent(NativeEntity entity, int typeID) {

        return HasComponent != null && typeID >= 0 && HasComponent(entity, typeID) != 0;
    }

    // Componentの実行IDを返す
    internal static ulong ReadComponentInstanceID(NativeEntity entity, int typeID) {

        return GetComponentInstanceID != null && typeID >= 0 ? GetComponentInstanceID(entity, typeID) : 0;
    }

    // Componentの追加を予約する
    internal static void EnqueueAddComponent(NativeEntity entity, int typeID) {

        if (AddComponent != null && typeID >= 0) {
            AddComponent(entity, typeID);
        }
    }

    // Componentの削除を予約する
    internal static void EnqueueRemoveComponent(NativeEntity entity, int typeID) {

        if (RemoveComponent != null && typeID >= 0) {
            RemoveComponent(entity, typeID);
        }
    }

    // Bufferの要素数を返す
    internal static int ReadDynamicBufferLength(NativeEntity entity, int typeID, int elementSize) {

        return DynamicBufferLength != null ?
            DynamicBufferLength(entity, typeID, elementSize) : -1;
    }

    // Bufferの指定範囲をコピーする
    internal static int CopyDynamicBuffer(NativeEntity entity, int typeID, int elementSize, int startIndex,
        void* destination, int capacity) {

        return DynamicBufferCopy != null ?
            DynamicBufferCopy(
                entity, typeID, elementSize,
                startIndex, destination, capacity) : -1;
    }

    // Bufferの要素を変更する
    internal static bool MutateDynamicBuffer(NativeEntity entity, int typeID, int elementSize, int operation, int index,
        void* data, int count) {

        return DynamicBufferMutate != null &&
            DynamicBufferMutate(
                entity, typeID, elementSize,
                operation, index, data, count) != 0;
    }

    // Entityの破棄を予約する
    internal static void EnqueueDestroyEntity(NativeEntity entity) {

        if (DestroyEntity != null) {
            DestroyEntity(entity);
        }
    }

    // Scriptの有効状態を返す
    internal static int ReadScriptEnabled(NativeEntity owner, ulong scriptSlotID) {

        return GetScriptEnabled != null ? GetScriptEnabled(owner, scriptSlotID) : -1;
    }

    // Scriptの有効状態を設定する
    internal static void WriteScriptEnabled(NativeEntity owner, ulong scriptSlotID, bool enabled) {

        if (SetScriptEnabled != null) {
            SetScriptEnabled(owner, scriptSlotID, enabled ? 1 : 0);
        }
    }

    // 型IDからEntity上のScriptを探す
    internal static NativeScriptInstanceHandle FindScriptInstance(NativeEntity owner, string scriptTypeID) {

        if (GetScriptInstance == null || string.IsNullOrEmpty(scriptTypeID)) {
            return NativeScriptInstanceHandle.Null;
        }
        byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(scriptTypeID);
        fixed (byte* ptr = bytes) {
            return GetScriptInstance(owner, ptr);
        }
    }

    // EntityへScriptを追加する
    internal static NativeScriptInstanceHandle AttachScriptInstance(NativeEntity owner, string scriptTypeID) {

        if (AttachScript == null || string.IsNullOrEmpty(scriptTypeID)) {
            return NativeScriptInstanceHandle.Null;
        }
        byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(scriptTypeID);
        fixed (byte* ptr = bytes) {
            return AttachScript(owner, ptr);
        }
    }

    // 空のGameObjectを予約する
    internal static NativeEntity CreateGameObjectHandle(string name) {

        if (CreateEntity == null) {
            throw new InvalidOperationException("Native APIが接続されていません");
        }
        byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(name ?? string.Empty);
        fixed (byte* ptr = bytes) {
            return CreateEntity(ptr, NativeEntity.Null);
        }
    }

    // 名前と親を指定してGameObjectを予約する
    internal static GameObject? SpawnEntity(string? name, GameObject? parent) {

        if (CreateEntity == null) {
            return null;
        }
        byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes((name ?? string.Empty));
        fixed (byte* ptr = bytes) {
            return GameObject.FromNative(CreateEntity(ptr, GameObject.RawNative(parent)));
        }
    }

    // PrefabからGameObjectを生成する
    internal static GameObject? SpawnPrefab(AssetGUID prefabAssetID, Vector3 position, Quaternion rotation,
        bool useTransform, GameObject? parent) {

        if (InstantiatePrefab == null) {
            return null;
        }
        return GameObject.FromNative(InstantiatePrefab(prefabAssetID, NativeVector3.From(position),
            NativeQuaternion.From(rotation), useTransform ? 1 : 0, GameObject.RawNative(parent)));
    }

    // GameObjectの階層を複製する
    internal static GameObject? CloneEntity(GameObject source, Vector3 position, Quaternion rotation, bool useTransform,
        GameObject? parent) {

        if (InstantiateEntity == null) {
            return null;
        }
        return GameObject.FromNative(InstantiateEntity(source.native, NativeVector3.From(position),
            NativeQuaternion.From(rotation), useTransform ? 1 : 0, GameObject.RawNative(parent)));
    }

    // 所有Sceneを含めて保存参照を解決する
    internal static GameObject? ResolveEntityReference(AssetGUID sourceAsset, ulong localFileID, NativeEntity owner)
        => (ResolveEntityRef != null && localFileID != 0) ?
            GameObject.FromNative(ResolveEntityRef(sourceAsset, localFileID, owner)) : null;

    // Entityの保存参照IDを取得する
    internal static EntityRef ReadEntityReferenceIdentity(NativeEntity entity) {

        if (GetEntityReferenceIdentity == null) {
            return EntityRef.Null;
        }
        AssetGUID sourceAsset = AssetGUID.None;
        ulong localFileID = 0;
        int kind = 0;
        GetEntityReferenceIdentity(entity, &sourceAsset, &localFileID, &kind);
        return new EntityRef((EntityRefKind)kind, sourceAsset, new UUID(localFileID));
    }

    // Sceneの追加読込を予約する
    internal static ulong SceneLoadAdditive(AssetGUID sceneAssetID) =>
        LoadSceneAdditive != null ? LoadSceneAdditive(sceneAssetID) : 0ul;

    // Sceneの置換読込を予約する
    internal static ulong SceneLoadSingle(AssetGUID sceneAssetID) =>
        LoadSceneSingle != null ? LoadSceneSingle(sceneAssetID) : 0ul;

    // Active Sceneの再読込を予約する
    internal static ulong SceneReloadActive() => ReloadActiveScene != null ? ReloadActiveScene() : 0ul;

    // Sceneの解放を予約する
    internal static void SceneUnload(ulong sceneInstanceID) {

        if (UnloadScene != null) {
            UnloadScene(sceneInstanceID);
        }
    }

    // Scene Instanceの生存状態を返す
    internal static bool SceneInstanceAlive(ulong sceneInstanceID) =>
        IsSceneInstanceAlive != null && IsSceneInstanceAlive(sceneInstanceID) != 0;

    // ワールド座標の保持を指定して親を変更する
    internal static void ReparentKeepWorld(NativeEntity child, NativeEntity parent, bool worldPositionStays) {

        if (SetParentKeepWorld != null) {
            SetParentKeepWorld(child, parent, worldPositionStays ? 1 : 0);
        }
    }

    // EntityのTagを読む
    internal static string ReadTag(NativeEntity entity) {

        if (CopyTag == null) {
            return "Untagged";
        }
        string tag = ManagedUTF8Transfer.ReadEntityString(CopyTag, entity);
        return tag.Length == 0 ? "Untagged" : tag;
    }

    // EntityのTagを設定する
    internal static void WriteTag(NativeEntity entity, string value) {

        if (SetTag == null) {
            return;
        }
        string safe = value ?? "Untagged";
        byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(safe);
        fixed (byte* ptr = bytes) {
            SetTag(entity, ptr);
        }
    }

    // 描画Layerのマスクを返す
    internal static uint ReadVisibilityLayerMask(NativeEntity entity)
        => GetVisibilityLayerMask != null ? (uint)GetVisibilityLayerMask(entity) : 0u;

    // 描画Layerのマスクを設定する
    internal static void WriteVisibilityLayerMask(NativeEntity entity, uint mask) {

        if (SetVisibilityLayerMask != null) {
            SetVisibilityLayerMask(entity, (int)mask);
        }
    }

    // 名前からGameObjectを探す
    internal static GameObject? FindByName(string name) {

        if (FindEntityByName == null || string.IsNullOrEmpty(name)) {
            return null;
        }
        byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(name ?? string.Empty);
        fixed (byte* ptr = bytes) {
            return GameObject.FromNative(FindEntityByName(ptr));
        }
    }

    // TagからGameObjectを探す
    internal static GameObject? FindByTag(string tag) {

        if (FindEntityByTag == null || string.IsNullOrEmpty(tag)) {
            return null;
        }
        byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(tag);
        fixed (byte* ptr = bytes) {
            return GameObject.FromNative(FindEntityByTag(ptr));
        }
    }

    // Tagに一致するGameObjectを列挙する
    internal static GameObject[] FindManyByTag(string tag) {

        if (FindEntitiesByTag == null || string.IsNullOrEmpty(tag)) {
            return System.Array.Empty<GameObject>();
        }
        byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(tag);
        fixed (byte* ptr = bytes) {
            // 件数を問い合わせて検索結果を取得する
            int count = FindEntitiesByTag(ptr, null, 0);
            if (count <= 0) {
                return System.Array.Empty<GameObject>();
            }
            var buffer = new NativeEntity[count];
            fixed (NativeEntity* bp = buffer) {
                int written = FindEntitiesByTag(ptr, bp, count);
                return MakeEntityArray(buffer, written < count ? written : count);
            }
        }
    }

    // Componentを持つGameObjectを探す
    internal static GameObject? FindByComponent(int typeID) {

        if (FindEntityByComponent == null || typeID < 0) {
            return null;
        }
        return GameObject.FromNative(FindEntityByComponent(typeID));
    }

    // Componentを持つGameObjectを列挙する
    internal static GameObject[] FindManyByComponent(int typeID) {

        if (FindEntitiesByComponent == null || typeID < 0) {
            return System.Array.Empty<GameObject>();
        }
        int count = FindEntitiesByComponent(typeID, null, 0);
        if (count <= 0) {
            return System.Array.Empty<GameObject>();
        }
        var buffer = new NativeEntity[count];
        fixed (NativeEntity* bp = buffer) {
            int written = FindEntitiesByComponent(typeID, bp, count);
            return MakeEntityArray(buffer, written < count ? written : count);
        }
    }

    // 生存中のGameObjectを検索結果へ詰める
    private static GameObject[] MakeEntityArray(NativeEntity[] buffer, int count) {

        if (count < 0 || count > buffer.Length) {
            throw new InvalidOperationException("GameObjectの検索件数が不正です");
        }
        var result = new GameObject[count];
        int written = 0;
        for (int i = 0; i < count; ++i) {
            GameObject? target = GameObject.FromNative(buffer[i]);
            if (target is not null) {
                result[written++] = target;
            }
        }
        if (written != count) {
            Array.Resize(ref result, written);
        }
        return result;
    }
}
