namespace NEMEngine;

using static NEMEngine.NativeAPI;

// 接続済みcallbackを用途別に呼び出す
internal static unsafe class NativePhysicsAPI {

    // Colliderの整数値を返す
    internal static int CollisionGetShapeInt(NativeEntity entity, int propertyID) => ReadShape<int>(entity, propertyID);
    // Colliderの実数値を返す
    internal static float CollisionGetShapeFloat(NativeEntity entity, int propertyID) =>
        ReadShape<float>(entity, propertyID);
    // Colliderの2軸値を返す
    internal static Vector2 CollisionGetShapeVector2(NativeEntity entity, int propertyID) =>
        ReadShape<Vector2>(entity, propertyID);
    // Colliderの3軸値を返す
    internal static Vector3 CollisionGetShapeVector3(NativeEntity entity, int propertyID) =>
        ReadShape<Vector3>(entity, propertyID);
    // Colliderの整数値を設定する
    internal static void CollisionSetShapeInt(NativeEntity entity, int propertyID, int value) =>
        WriteShape(entity, propertyID, value);
    // Colliderの実数値を設定する
    internal static void CollisionSetShapeFloat(NativeEntity entity, int propertyID, float value) =>
        WriteShape(entity, propertyID, value);
    // Colliderの2軸値を設定する
    internal static void CollisionSetShapeVector2(NativeEntity entity, int propertyID, Vector2 value) =>
        WriteShape(entity, propertyID, value);
    // Colliderの3軸値を設定する
    internal static void CollisionSetShapeVector3(NativeEntity entity, int propertyID, Vector3 value) =>
        WriteShape(entity, propertyID, value);

    // レイの最近接触を取得する
    internal static bool RaycastClosest(Vector3 origin, Vector3 direction, float maxDistance, uint layerMask,
        uint targets, QueryTriggerInteraction triggerInteraction, out NativeRaycastHit hit) {

        hit = default;
        if (PhysicsRaycast == null) {
            return false;
        }
        fixed (NativeRaycastHit* hitPtr = &hit) {
            return PhysicsRaycast(NativeVector3.From(origin), NativeVector3.From(direction),
                maxDistance, layerMask, targets, (uint)triggerInteraction, hitPtr) != 0;
        }
    }

    // Triggerを含む既定の検索条件を返す
    internal static bool ReadQueriesHitTriggers()
        => GetQueriesHitTriggers != null && GetQueriesHitTriggers() != 0;

    // Triggerを含む既定の検索条件を設定する
    internal static void WriteQueriesHitTriggers(bool value) {

        if (SetQueriesHitTriggers != null) {
            SetQueriesHitTriggers(value ? 1 : 0);
        }
    }

    // レイの全接触を取得する
    internal static int RaycastMany(Vector3 origin, Vector3 direction, float maxDistance, uint layerMask, uint targets,
        QueryTriggerInteraction triggerInteraction, Span<NativeRaycastHit> buffer) {

        if (PhysicsRaycastAll == null) {
            return 0;
        }
        fixed (NativeRaycastHit* bufferPtr = buffer) {
            return PhysicsRaycastAll(NativeVector3.From(origin), NativeVector3.From(direction),
                maxDistance, layerMask, targets, (uint)triggerInteraction,
                bufferPtr, buffer.Length);
        }
    }

    // Collisionのマスクを取得する
    internal static uint ReadCollisionTypeMask(string name) {

        if (GetCollisionTypeMaskByName == null || string.IsNullOrEmpty(name)) {
            return 0;
        }
        byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(name ?? string.Empty);
        fixed (byte* ptr = bytes) {
            return GetCollisionTypeMaskByName(ptr);
        }
    }

    // Collisionのマスクを取得する
    internal static uint ReadCollisionTypeMask(NativeEntity entity)
        => GetCollisionTypeMask != null ? (uint)GetCollisionTypeMask(entity) : 0u;

    // Collisionの接触状態を返す
    internal static bool ReadCollisionRuntimeState(NativeEntity entity)
        => GetCollisionRuntimeState != null && GetCollisionRuntimeState(entity) != 0;

    // Collisionのマスクを設定する
    internal static void WriteCollisionTypeMask(NativeEntity entity, uint mask) {

        if (SetCollisionTypeMask != null) {
            SetCollisionTypeMask(entity, (int)mask);
        }
    }

    // Colliderの形状値を取得する
    private static T ReadShape<T>(NativeEntity entity, int propertyID) where T : unmanaged {

        T value = default;
        if (CollisionGetShapeProperty == null || CollisionGetShapeProperty(entity, propertyID, &value, sizeof(T)) == 0) {
            throw new InvalidOperationException($"Collision形状を取得できません: property={propertyID}");
        }
        return value;
    }

    // Colliderの形状値を設定する
    private static void WriteShape<T>(NativeEntity entity, int propertyID, T value) where T : unmanaged {

        if (CollisionSetShapeProperty == null || CollisionSetShapeProperty(entity, propertyID, &value, sizeof(T)) == 0) {
            throw new InvalidOperationException($"Collision形状を設定できません: property={propertyID}");
        }
    }
}
