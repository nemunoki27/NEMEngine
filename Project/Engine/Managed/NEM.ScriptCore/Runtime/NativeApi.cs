using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace NEMEngine;

[StructLayout(LayoutKind.Sequential)]
public struct NativeVector3 {

    public float x;
    public float y;
    public float z;

    public static NativeVector3 From(Vector3 value) {
        return new NativeVector3 {
            x = value.x,
            y = value.y,
            z = value.z
        };
    }

    public Vector3 ToVector3() {
        return new Vector3(x, y, z);
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeQuaternion {

    public float x;
    public float y;
    public float z;
    public float w;

    public static NativeQuaternion From(Quaternion value) {
        return new NativeQuaternion {
            x = value.x,
            y = value.y,
            z = value.z,
            w = value.w
        };
    }

    public Quaternion ToQuaternion() {
        return new Quaternion(x, y, z, w);
    }
}

internal static unsafe class NativeApi {

    internal static delegate* unmanaged[Cdecl]<float> GetDeltaTime;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> GetPosition;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3, void> SetPosition;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> GetLocalPosition;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3, void> SetLocalPosition;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> GetLocalScale;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3, void> SetLocalScale;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeQuaternion> GetLocalRotation;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeQuaternion, void> SetLocalRotation;

    internal static void SetCallbacks(NativeApiTable* callbacks) {

        // C++側から渡されたECSアクセス用の関数テーブルを保持する
        GetDeltaTime = callbacks->getDeltaTime;
        GetPosition = callbacks->getPosition;
        SetPosition = callbacks->setPosition;
        GetLocalPosition = callbacks->getLocalPosition;
        SetLocalPosition = callbacks->setLocalPosition;
        GetLocalScale = callbacks->getLocalScale;
        SetLocalScale = callbacks->setLocalScale;
        GetLocalRotation = callbacks->getLocalRotation;
        SetLocalRotation = callbacks->setLocalRotation;
    }

    internal static float ReadDeltaTime() {

        // ランタイム未初期化時はスクリプトを安全に動かさず0秒として扱う
        return GetDeltaTime != null ? GetDeltaTime() : 0.0f;
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
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeApiTable {

    public delegate* unmanaged[Cdecl]<float> getDeltaTime;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> getPosition;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3, void> setPosition;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> getLocalPosition;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3, void> setLocalPosition;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> getLocalScale;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3, void> setLocalScale;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeQuaternion> getLocalRotation;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeQuaternion, void> setLocalRotation;
}
