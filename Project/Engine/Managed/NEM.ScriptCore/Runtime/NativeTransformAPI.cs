namespace NEMEngine;

using static NEMEngine.NativeAPI;

// 接続済みcallbackを用途別に呼び出す
internal static unsafe class NativeTransformAPI {

    // 親回転の継承除外状態を返す
    internal static bool ReadIgnoreParentRotation(NativeEntity entity) {

        return GetIgnoreParentRotation != null && GetIgnoreParentRotation(entity) != 0;
    }

    // 親回転の継承を切り替える
    internal static void WriteIgnoreParentRotation(NativeEntity entity, bool value) {

        if (SetIgnoreParentRotation != null) {
            SetIgnoreParentRotation(entity, value ? 1 : 0);
        }
    }

    // 親Scaleの継承除外状態を返す
    internal static bool ReadIgnoreParentScale(NativeEntity entity) {

        return GetIgnoreParentScale != null && GetIgnoreParentScale(entity) != 0;
    }

    // 親Scaleの継承を切り替える
    internal static void WriteIgnoreParentScale(NativeEntity entity, bool value) {

        if (SetIgnoreParentScale != null) {
            SetIgnoreParentScale(entity, value ? 1 : 0);
        }
    }

    // 親のGameObjectを返す
    internal static GameObject? ReadParent(NativeEntity entity) {

        return GameObject.FromNative(GetParent != null ? GetParent(entity) : NativeEntity.Null);
    }

    // 先頭の子を返す
    internal static GameObject? ReadFirstChild(NativeEntity entity) {

        return GameObject.FromNative(GetFirstChild != null ? GetFirstChild(entity) : NativeEntity.Null);
    }

    // 次の兄弟を返す
    internal static GameObject? ReadNextSibling(NativeEntity entity) {

        return GameObject.FromNative(GetNextSibling != null ? GetNextSibling(entity) : NativeEntity.Null);
    }

    // 親を変更する
    internal static void WriteParent(NativeEntity entity, NativeEntity parent) {

        if (SetParent != null) {
            SetParent(entity, parent);
        }
    }

    // ワールド位置を返す
    internal static Vector3 ReadPosition(NativeEntity entity) {

        // NativeのTransform値を取得する
        return GetPosition != null ? GetPosition(entity).ToVector3() : Vector3.zero;
    }

    // ワールド位置を設定する
    internal static void WritePosition(NativeEntity entity, Vector3 value) {

        if (SetPosition != null) {

            // Nativeで値と変更通知を更新する
            SetPosition(entity, NativeVector3.From(value));
        }
    }

    // ローカル位置を返す
    internal static Vector3 ReadLocalPosition(NativeEntity entity) {

        return GetLocalPosition != null ? GetLocalPosition(entity).ToVector3() : Vector3.zero;
    }

    // ローカル位置を設定する
    internal static void WriteLocalPosition(NativeEntity entity, Vector3 value) {

        if (SetLocalPosition != null) {
            SetLocalPosition(entity, NativeVector3.From(value));
        }
    }

    // ローカルScaleを返す
    internal static Vector3 ReadLocalScale(NativeEntity entity) {

        return GetLocalScale != null ? GetLocalScale(entity).ToVector3() : Vector3.one;
    }

    // ローカルScaleを設定する
    internal static void WriteLocalScale(NativeEntity entity, Vector3 value) {

        if (SetLocalScale != null) {
            SetLocalScale(entity, NativeVector3.From(value));
        }
    }

    // ローカル回転を返す
    internal static Quaternion ReadLocalRotation(NativeEntity entity) {

        return GetLocalRotation != null ? GetLocalRotation(entity).ToQuaternion() : Quaternion.identity;
    }

    // ローカル回転を設定する
    internal static void WriteLocalRotation(NativeEntity entity, Quaternion value) {

        if (SetLocalRotation != null) {
            SetLocalRotation(entity, NativeQuaternion.From(value));
        }
    }

    // ワールド回転を返す
    internal static Quaternion ReadRotation(NativeEntity entity) {

        return GetRotation != null ? GetRotation(entity).ToQuaternion() : Quaternion.identity;
    }

    // ワールド回転を設定する
    internal static void WriteRotation(NativeEntity entity, Quaternion value) {

        if (SetRotation != null) {
            SetRotation(entity, NativeQuaternion.From(value));
        }
    }

    // ワールドScaleを返す
    internal static Vector3 ReadLossyScale(NativeEntity entity) {

        return GetLossyScale != null ? GetLossyScale(entity).ToVector3() : Vector3.one;
    }

    // 画面座標からワールドレイを作る
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

    // ワールド座標を画面座標へ変換する
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
