using System.Text;

namespace NEMEngine;

using static NEMEngine.NativeAPI;

// 接続済みcallbackを用途別に呼び出す
internal static unsafe class NativePhysicsAPI {

    internal static int CollisionGetShapeInt(NativeEntity entity, int propertyID) {
        int v = 0;
        if (CollisionGetShapeProperty != null) { CollisionGetShapeProperty(entity, propertyID, &v, 4); }
        return v;
    }

    internal static void CollisionSetShapeInt(NativeEntity entity, int propertyID, int value) {
        if (CollisionSetShapeProperty != null) { CollisionSetShapeProperty(entity, propertyID, &value, 4); }
    }

    internal static float CollisionGetShapeFloat(NativeEntity entity, int propertyID) {
        float v = 0.0f;
        if (CollisionGetShapeProperty != null) { CollisionGetShapeProperty(entity, propertyID, &v, 4); }
        return v;
    }

    internal static void CollisionSetShapeFloat(NativeEntity entity, int propertyID, float value) {
        if (CollisionSetShapeProperty != null) { CollisionSetShapeProperty(entity, propertyID, &value, 4); }
    }

    internal static Vector2 CollisionGetShapeVector2(NativeEntity entity, int propertyID) {
        Vector2 v = default;
        if (CollisionGetShapeProperty != null) { CollisionGetShapeProperty(entity, propertyID, &v, 8); }
        return v;
    }

    internal static void CollisionSetShapeVector2(NativeEntity entity, int propertyID, Vector2 value) {
        if (CollisionSetShapeProperty != null) { CollisionSetShapeProperty(entity, propertyID, &value, 8); }
    }

    internal static Vector3 CollisionGetShapeVector3(NativeEntity entity, int propertyID) {
        Vector3 v = default;
        if (CollisionGetShapeProperty != null) { CollisionGetShapeProperty(entity, propertyID, &v, 12); }
        return v;
    }

    internal static void CollisionSetShapeVector3(NativeEntity entity, int propertyID, Vector3 value) {
        if (CollisionSetShapeProperty != null) { CollisionSetShapeProperty(entity, propertyID, &value, 12); }
    }

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
