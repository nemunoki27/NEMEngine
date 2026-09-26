using System.Text;

namespace NEMEngine;

using static NEMEngine.NativeAPI;

// 接続済みcallbackを用途別に呼び出す
internal static unsafe class NativePhysicsAPI {

    // 取得・設定に失敗した形状値を既定値で隠さない
    private static T ReadShape<T>(NativeEntity entity, int propertyID) where T : unmanaged {
        T value = default;
        if (CollisionGetShapeProperty == null || CollisionGetShapeProperty(entity, propertyID, &value, sizeof(T)) == 0) {
            throw new InvalidOperationException($"Collision形状を取得できません: property={propertyID}");
        }
        return value;
    }

    private static void WriteShape<T>(NativeEntity entity, int propertyID, T value) where T : unmanaged {
        if (CollisionSetShapeProperty == null || CollisionSetShapeProperty(entity, propertyID, &value, sizeof(T)) == 0) {
            throw new InvalidOperationException($"Collision形状を設定できません: property={propertyID}");
        }
    }

    internal static int CollisionGetShapeInt(NativeEntity entity, int propertyID) => ReadShape<int>(entity, propertyID);
    internal static float CollisionGetShapeFloat(NativeEntity entity, int propertyID) => ReadShape<float>(entity, propertyID);
    internal static Vector2 CollisionGetShapeVector2(NativeEntity entity, int propertyID) => ReadShape<Vector2>(entity, propertyID);
    internal static Vector3 CollisionGetShapeVector3(NativeEntity entity, int propertyID) => ReadShape<Vector3>(entity, propertyID);
    internal static void CollisionSetShapeInt(NativeEntity entity, int propertyID, int value) => WriteShape(entity, propertyID, value);
    internal static void CollisionSetShapeFloat(NativeEntity entity, int propertyID, float value) => WriteShape(entity, propertyID, value);
    internal static void CollisionSetShapeVector2(NativeEntity entity, int propertyID, Vector2 value) => WriteShape(entity, propertyID, value);
    internal static void CollisionSetShapeVector3(NativeEntity entity, int propertyID, Vector3 value) => WriteShape(entity, propertyID, value);

    internal static bool RaycastClosest(Vector3 origin, Vector3 direction, float maxDistance,
        uint layerMask, uint targets, out NativeRaycastHit hit) {

        hit = default;
        if (PhysicsRaycast == null) {
            return false;
        }
        fixed (NativeRaycastHit* hitPtr = &hit) {
            return PhysicsRaycast(NativeVector3.From(origin), NativeVector3.From(direction),
                maxDistance, layerMask, targets, hitPtr) != 0;
        }
    }

    internal static int RaycastMany(Vector3 origin, Vector3 direction, float maxDistance,
        uint layerMask, uint targets, Span<NativeRaycastHit> buffer) {

        if (PhysicsRaycastAll == null) {
            return 0;
        }
        fixed (NativeRaycastHit* bufferPtr = buffer) {
            return PhysicsRaycastAll(NativeVector3.From(origin), NativeVector3.From(direction),
                maxDistance, layerMask, targets, bufferPtr, buffer.Length);
        }
    }

    internal static uint ReadCollisionTypeMask(string name) {

        if (GetCollisionTypeMaskByName == null || string.IsNullOrEmpty(name)) {
            return 0;
        }
        byte[] bytes = Encoding.UTF8.GetBytes(name + "\0");
        fixed (byte* ptr = bytes) {
            return GetCollisionTypeMaskByName(ptr);
        }
    }

    internal static uint ReadCollisionTypeMask(NativeEntity entity)
        => GetCollisionTypeMask != null ? (uint)GetCollisionTypeMask(entity) : 0u;

    internal static bool ReadCollisionRuntimeState(NativeEntity entity)
        => GetCollisionRuntimeState != null && GetCollisionRuntimeState(entity) != 0;

    internal static void WriteCollisionTypeMask(NativeEntity entity, uint mask) {
        if (SetCollisionTypeMask != null) { SetCollisionTypeMask(entity, (int)mask); }
    }
}
