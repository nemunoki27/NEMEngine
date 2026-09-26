using System.Text;

namespace NEMEngine;

using static NEMEngine.NativeAPI;

// 接続済みcallbackを用途別に呼び出す
internal static unsafe class NativeEntityAPI {

    internal static bool ReadIsAlive(NativeEntity entity) {
        return IsAlive != null && IsAlive(entity) != 0;
    }

    internal static string ReadName(NativeEntity entity) {
        if (CopyName == null) {
            return string.Empty;
        }

        return ManagedUTF8Transfer.ReadEntityString(CopyName, entity);
    }

    internal static void WriteName(NativeEntity entity, string value) {
        if (SetName == null) {
            return;
        }

        string safeValue = value ?? string.Empty;
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(safeValue) + 1];
        Encoding.UTF8.GetBytes(safeValue, 0, safeValue.Length, bytes, 0);
        fixed (byte* ptr = bytes) {
            SetName(entity, ptr);
        }
    }

    internal static bool ReadActiveSelf(NativeEntity entity) {
        return GetActiveSelf == null || GetActiveSelf(entity) != 0;
    }

    internal static void WriteActiveSelf(NativeEntity entity, bool value) {
        if (SetActiveSelf != null) {
            SetActiveSelf(entity, value ? 1 : 0);
        }
    }

    internal static bool ReadActiveInHierarchy(NativeEntity entity) {
        return GetActiveInHierarchy != null && GetActiveInHierarchy(entity) != 0;
    }

    internal static bool ReadHasComponent(NativeEntity entity, int typeID) {
        return HasComponent != null && typeID >= 0 && HasComponent(entity, typeID) != 0;
    }

    internal static ulong ReadComponentInstanceID(NativeEntity entity, int typeID) {

        return GetComponentInstanceID != null && typeID >= 0 ? GetComponentInstanceID(entity, typeID) : 0;
    }

    internal static void EnqueueAddComponent(NativeEntity entity, int typeID) {
        if (AddComponent != null && typeID >= 0) {
            AddComponent(entity, typeID);
        }
    }

    internal static void EnqueueRemoveComponent(NativeEntity entity, int typeID) {
        if (RemoveComponent != null && typeID >= 0) {
            RemoveComponent(entity, typeID);
        }
    }

    internal static int ReadDynamicBufferLength(
        NativeEntity entity, int typeID, int elementSize) {

        return DynamicBufferLength != null ?
            DynamicBufferLength(entity, typeID, elementSize) : -1;
    }

    internal static int CopyDynamicBuffer(
        NativeEntity entity, int typeID, int elementSize,
        int startIndex, void* destination, int capacity) {

        return DynamicBufferCopy != null ?
            DynamicBufferCopy(
                entity, typeID, elementSize,
                startIndex, destination, capacity) : -1;
    }

    internal static bool MutateDynamicBuffer(
        NativeEntity entity, int typeID, int elementSize,
        int operation, int index, void* data, int count) {

        return DynamicBufferMutate != null &&
            DynamicBufferMutate(
                entity, typeID, elementSize,
                operation, index, data, count) != 0;
    }

    internal static void EnqueueDestroyEntity(NativeEntity entity) {
        if (DestroyEntity != null) {
            DestroyEntity(entity);
        }
    }

    internal static int ReadScriptEnabled(NativeEntity owner, ulong scriptSlotID) {
        return GetScriptEnabled != null ? GetScriptEnabled(owner, scriptSlotID) : -1;
    }

    internal static void WriteScriptEnabled(NativeEntity owner, ulong scriptSlotID, bool enabled) {
        if (SetScriptEnabled != null) {
            SetScriptEnabled(owner, scriptSlotID, enabled ? 1 : 0);
        }
    }

    internal static NativeScriptInstanceHandle FindScriptInstance(NativeEntity owner, string scriptTypeID) {
        if (GetScriptInstance == null || string.IsNullOrEmpty(scriptTypeID)) {
            return NativeScriptInstanceHandle.Null;
        }
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(scriptTypeID) + 1];
        Encoding.UTF8.GetBytes(scriptTypeID, 0, scriptTypeID.Length, bytes, 0);
        fixed (byte* ptr = bytes) {
            return GetScriptInstance(owner, ptr);
        }
    }

    internal static bool TryAttachScript(NativeEntity owner, string scriptTypeID) {
        if (AttachScript == null || string.IsNullOrEmpty(scriptTypeID)) {
            return false;
        }
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(scriptTypeID) + 1];
        Encoding.UTF8.GetBytes(scriptTypeID, 0, scriptTypeID.Length, bytes, 0);
        fixed (byte* ptr = bytes) {
            return AttachScript(owner, ptr) != 0;
        }
    }

    internal static NativeEntity CreateGameObjectHandle(string name) {
        if (CreateEntity == null) {
            throw new InvalidOperationException("Native APIが接続されていません");
        }
        byte[] bytes = Encoding.UTF8.GetBytes(name + "\0");
        fixed (byte* ptr = bytes) {
            return CreateEntity(ptr, NativeEntity.Null);
        }
    }

    internal static GameObject? SpawnEntity(string? name, GameObject? parent) {
        if (CreateEntity == null) {
            return null;
        }
        byte[] bytes = Encoding.UTF8.GetBytes((name ?? string.Empty) + "\0");
        fixed (byte* ptr = bytes) {
            return GameObject.FromNative(CreateEntity(ptr, GameObject.RawNative(parent)));
        }
    }

    internal static GameObject? SpawnPrefab(AssetGUID prefabAssetID, Vector3 position, Quaternion rotation, bool useTransform, GameObject? parent) {
        if (InstantiatePrefab == null) {
            return null;
        }
        return GameObject.FromNative(InstantiatePrefab(prefabAssetID, NativeVector3.From(position),
            NativeQuaternion.From(rotation), useTransform ? 1 : 0, GameObject.RawNative(parent)));
    }

    internal static GameObject? ResolveEntityReference(AssetGUID sourceAsset, ulong localFileID)
        => (ResolveEntityRef != null && localFileID != 0) ? GameObject.FromNative(ResolveEntityRef(sourceAsset, localFileID)) : null;

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

    internal static ulong SceneLoadAdditive(AssetGUID sceneAssetID) => LoadSceneAdditive != null ? LoadSceneAdditive(sceneAssetID) : 0ul;

    internal static ulong SceneLoadSingle(AssetGUID sceneAssetID) => LoadSceneSingle != null ? LoadSceneSingle(sceneAssetID) : 0ul;

    internal static ulong SceneReloadActive() => ReloadActiveScene != null ? ReloadActiveScene() : 0ul;

    internal static void SceneUnload(ulong sceneInstanceID) { if (UnloadScene != null) { UnloadScene(sceneInstanceID); } }

    internal static bool SceneInstanceAlive(ulong sceneInstanceID) => IsSceneInstanceAlive != null && IsSceneInstanceAlive(sceneInstanceID) != 0;

    internal static void ReparentKeepWorld(NativeEntity child, NativeEntity parent, bool worldPositionStays) {
        if (SetParentKeepWorld != null) { SetParentKeepWorld(child, parent, worldPositionStays ? 1 : 0); }
    }

    internal static string ReadTag(NativeEntity entity) {
        if (CopyTag == null) {
            return "Untagged";
        }
        string tag = ManagedUTF8Transfer.ReadEntityString(CopyTag, entity);
        return tag.Length == 0 ? "Untagged" : tag;
    }

    internal static void WriteTag(NativeEntity entity, string value) {
        if (SetTag == null) {
            return;
        }
        string safe = value ?? "Untagged";
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(safe) + 1];
        Encoding.UTF8.GetBytes(safe, 0, safe.Length, bytes, 0);
        fixed (byte* ptr = bytes) {
            SetTag(entity, ptr);
        }
    }

    internal static uint ReadVisibilityLayerMask(NativeEntity entity)
        => GetVisibilityLayerMask != null ? (uint)GetVisibilityLayerMask(entity) : 0u;

    internal static void WriteVisibilityLayerMask(NativeEntity entity, uint mask) {
        if (SetVisibilityLayerMask != null) { SetVisibilityLayerMask(entity, (int)mask); }
    }

    internal static GameObject? FindByName(string name) {
        if (FindEntityByName == null || string.IsNullOrEmpty(name)) {
            return null;
        }
        byte[] bytes = Encoding.UTF8.GetBytes(name + "\0");
        fixed (byte* ptr = bytes) {
            return GameObject.FromNative(FindEntityByName(ptr));
        }
    }

    internal static GameObject? FindByTag(string tag) {
        if (FindEntityByTag == null || string.IsNullOrEmpty(tag)) {
            return null;
        }
        byte[] bytes = Encoding.UTF8.GetBytes(tag + "\0");
        fixed (byte* ptr = bytes) {
            return GameObject.FromNative(FindEntityByTag(ptr));
        }
    }

    internal static GameObject[] FindManyByTag(string tag) {
        if (FindEntitiesByTag == null || string.IsNullOrEmpty(tag)) {
            return System.Array.Empty<GameObject>();
        }
        byte[] bytes = Encoding.UTF8.GetBytes(tag + "\0");
        fixed (byte* ptr = bytes) {
            // length-query で総数を得てから確保し、再取得して詰める
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

    internal static GameObject? FindByComponent(int typeID) {
        if (FindEntityByComponent == null || typeID < 0) {
            return null;
        }
        return GameObject.FromNative(FindEntityByComponent(typeID));
    }

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

    private static GameObject[] MakeEntityArray(NativeEntity[] buffer, int count) {
        if (count < 0 || count > buffer.Length) {
            throw new InvalidOperationException("GameObjectの検索件数が不正です");
        }
        var result = new GameObject[count];
        int written = 0;
        for (int i = 0; i < count; ++i) {
            GameObject? target = GameObject.FromNative(buffer[i]);
            if (target is not null) { result[written++] = target; }
        }
        if (written != count) { Array.Resize(ref result, written); }
        return result;
    }
}
