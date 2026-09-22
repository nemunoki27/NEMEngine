namespace NEMEngine;

// ワールド空間のレイ。directionは正規化して使う
public struct Ray {

    public Vector3 origin;
    public Vector3 direction;

    public Ray(Vector3 origin, Vector3 direction) {
        this.origin = origin;
        this.direction = direction;
    }

    // origin から distance だけ進んだ点を返す
    public Vector3 GetPoint(float distance) => origin + direction * distance;
}

// レイキャストのヒット結果
public struct RaycastHit {

    // ヒットしたコライダーの所有Entity
    public Entity entity;

    // ワールド空間のヒット点と法線
    public Vector3 point;
    public Vector3 normal;
    // レイのoriginからの距離
    public float distance;

    // CollisionComponent内の形状index
    public int shapeIndex;
    // Trigger形状へのヒットか
    public bool isTrigger;

    public Transform transform => entity.transform;

    internal static RaycastHit From(NativeRaycastHit native) {
        return new RaycastHit {
            entity = new Entity(native.entity),
            point = native.point.ToVector3(),
            normal = native.normal.ToVector3(),
            distance = native.distance,
            shapeIndex = native.shapeIndex,
            isTrigger = native.trigger != 0,
        };
    }
}

// レイキャストの判定対象
[Flags]
public enum RaycastTargets : uint {

    // CollisionComponentの3D形状
    Colliders = 1 << 0,
    All = Colliders,
}

// 物理クエリ。コライダーの3D形状に対してレイを飛ばす。
// 判定はコライダー基準(見た目のメッシュではない)で、Transformは前フレームのLateUpdate確定値を参照する。
public static class Physics {

    // 全レイヤーを対象にするマスク
    public const uint AllLayers = 0xFFFFFFFFu;

    // 最近ヒットの有無だけを返す
    public static bool Raycast(Vector3 origin, Vector3 direction,
        float maxDistance = float.PositiveInfinity, uint layerMask = AllLayers, RaycastTargets targets = RaycastTargets.All) {
        return NativePhysicsAPI.RaycastClosest(origin, direction, maxDistance, layerMask, (uint)targets, out _);
    }

    // 最近ヒットを取得する、ヒット無しはfalse
    public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hit,
        float maxDistance = float.PositiveInfinity, uint layerMask = AllLayers, RaycastTargets targets = RaycastTargets.All) {

        if (NativePhysicsAPI.RaycastClosest(origin, direction, maxDistance, layerMask, (uint)targets, out NativeRaycastHit native)) {
            hit = RaycastHit.From(native);
            return true;
        }
        hit = default;
        return false;
    }

    public static bool Raycast(Ray ray, out RaycastHit hit,
        float maxDistance = float.PositiveInfinity, uint layerMask = AllLayers, RaycastTargets targets = RaycastTargets.All) {
        return Raycast(ray.origin, ray.direction, out hit, maxDistance, layerMask, targets);
    }

    // 2点間の線分でレイキャストする
    public static bool Linecast(Vector3 start, Vector3 end, out RaycastHit hit,
        uint layerMask = AllLayers, RaycastTargets targets = RaycastTargets.All) {

        Vector3 delta = end - start;
        float length = Vector3.Length(delta);
        if (length <= 0.0001f) {
            hit = default;
            return false;
        }
        return Raycast(start, delta * (1.0f / length), out hit, length, layerMask, targets);
    }

    // 全ヒットを距離昇順で返す
    public static RaycastHit[] RaycastAll(Vector3 origin, Vector3 direction,
        float maxDistance = float.PositiveInfinity, uint layerMask = AllLayers, RaycastTargets targets = RaycastTargets.All) {

        // 総数がbufferを超えた場合だけ広げて取り直す
        var buffer = new NativeRaycastHit[64];
        int total = NativePhysicsAPI.RaycastMany(origin, direction, maxDistance, layerMask, (uint)targets, buffer);
        if (buffer.Length < total) {
            buffer = new NativeRaycastHit[total];
            total = NativePhysicsAPI.RaycastMany(origin, direction, maxDistance, layerMask, (uint)targets, buffer);
        }

        int count = total < buffer.Length ? total : buffer.Length;
        var hits = new RaycastHit[count];
        for (int i = 0; i < count; ++i) {
            hits[i] = RaycastHit.From(buffer[i]);
        }
        return hits;
    }

    public static RaycastHit[] RaycastAll(Ray ray,
        float maxDistance = float.PositiveInfinity, uint layerMask = AllLayers, RaycastTargets targets = RaycastTargets.All) {
        return RaycastAll(ray.origin, ray.direction, maxDistance, layerMask, targets);
    }

    // Collisionタイプ名からレイヤーマスクを作る。未登録の名前は無視される
    public static uint GetLayerMask(params string[] typeNames) {

        uint mask = 0;
        foreach (string name in typeNames) {
            mask |= NativePhysicsAPI.ReadCollisionTypeMask(name);
        }
        return mask;
    }
}
