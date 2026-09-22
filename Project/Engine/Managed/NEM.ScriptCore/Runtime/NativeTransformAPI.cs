namespace NEMEngine;

using static NEMEngine.NativeAPI;

// 接続済みcallbackを用途別に呼び出す
internal static unsafe class NativeTransformAPI {

    internal static bool ReadIgnoreParentRotation(NativeEntity entity) {
        return GetIgnoreParentRotation != null && GetIgnoreParentRotation(entity) != 0;
    }

    internal static void WriteIgnoreParentRotation(NativeEntity entity, bool value) {
        if (SetIgnoreParentRotation != null) {
            SetIgnoreParentRotation(entity, value ? 1 : 0);
        }
    }

    internal static bool ReadIgnoreParentScale(NativeEntity entity) {
        return GetIgnoreParentScale != null && GetIgnoreParentScale(entity) != 0;
    }

    internal static void WriteIgnoreParentScale(NativeEntity entity, bool value) {
        if (SetIgnoreParentScale != null) {
            SetIgnoreParentScale(entity, value ? 1 : 0);
        }
    }

    internal static Entity ReadParent(NativeEntity entity) {
        return new Entity(GetParent != null ? GetParent(entity) : NativeEntity.Null);
    }

    internal static Entity ReadFirstChild(NativeEntity entity) {
        return new Entity(GetFirstChild != null ? GetFirstChild(entity) : NativeEntity.Null);
    }

    internal static Entity ReadNextSibling(NativeEntity entity) {
        return new Entity(GetNextSibling != null ? GetNextSibling(entity) : NativeEntity.Null);
    }

    internal static void WriteParent(NativeEntity entity, NativeEntity parent) {
        if (SetParent != null) {
            SetParent(entity, parent);
        }
    }

    internal static Vector3 ReadPosition(NativeEntity entity) {

        // Transformの実体はC++ ECS側にあるため、C#は値だけを取得する
        return GetPosition != null ? GetPosition(entity).ToVector3() : Vector3.zero;
    }

    internal static void WritePosition(NativeEntity entity, Vector3 value) {
        if (SetPosition != null) {

            // 値の適用とdirty化はC++側でまとめて行う
            SetPosition(entity, NativeVector3.From(value));
        }
    }

    internal static Vector3 ReadLocalPosition(NativeEntity entity) {
        return GetLocalPosition != null ? GetLocalPosition(entity).ToVector3() : Vector3.zero;
    }

    internal static void WriteLocalPosition(NativeEntity entity, Vector3 value) {
        if (SetLocalPosition != null) {
            SetLocalPosition(entity, NativeVector3.From(value));
        }
    }

    internal static Vector3 ReadLocalScale(NativeEntity entity) {
        return GetLocalScale != null ? GetLocalScale(entity).ToVector3() : Vector3.one;
    }

    internal static void WriteLocalScale(NativeEntity entity, Vector3 value) {
        if (SetLocalScale != null) {
            SetLocalScale(entity, NativeVector3.From(value));
        }
    }

    internal static Quaternion ReadLocalRotation(NativeEntity entity) {
        return GetLocalRotation != null ? GetLocalRotation(entity).ToQuaternion() : Quaternion.identity;
    }

    internal static void WriteLocalRotation(NativeEntity entity, Quaternion value) {
        if (SetLocalRotation != null) {
            SetLocalRotation(entity, NativeQuaternion.From(value));
        }
    }

    internal static Quaternion ReadRotation(NativeEntity entity) {
        return GetRotation != null ? GetRotation(entity).ToQuaternion() : Quaternion.identity;
    }

    internal static void WriteRotation(NativeEntity entity, Quaternion value) {
        if (SetRotation != null) {
            SetRotation(entity, NativeQuaternion.From(value));
        }
    }

    internal static Vector3 ReadLossyScale(NativeEntity entity) {
        return GetLossyScale != null ? GetLossyScale(entity).ToVector3() : Vector3.one;
    }

    internal static bool ReadScreenPointToRay(Vector2 screenPos, out Vector3 origin, out Vector3 direction) {

        origin = Vector3.zero;
        direction = Vector3.zero;
        if (ScreenPointToRay == null) {
            return false;
        }
        NativeVector3 nativeOrigin = default;
        NativeVector3 nativeDirection = default;
        if (ScreenPointToRay(screenPos.x, screenPos.y, &nativeOrigin, &nativeDirection) == 0) {
            return false;
        }
        origin = nativeOrigin.ToVector3();
        direction = nativeDirection.ToVector3();
        return true;
    }

    internal static bool ReadWorldToScreenPoint(Vector3 worldPosition, out Vector3 screenPosition) {

        screenPosition = Vector3.zero;
        if (WorldToScreenPoint == null) {
            return false;
        }
        NativeVector3 nativePosition = default;
        if (WorldToScreenPoint(NativeVector3.From(worldPosition), &nativePosition) == 0) {
            return false;
        }
        screenPosition = nativePosition.ToVector3();
        return true;
    }
}
