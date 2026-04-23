using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

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
public struct NativeVector2 {

    public float x;
    public float y;

    public Vector2 ToVector2() {
        return new Vector2(x, y);
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

    private const int NameBufferSize = 256;

    internal static delegate* unmanaged[Cdecl]<float> GetDeltaTime;
    internal static delegate* unmanaged[Cdecl]<float> GetFixedDeltaTime;
    internal static delegate* unmanaged[Cdecl]<int, byte*, void> Log;
    internal static delegate* unmanaged[Cdecl]<int, int> GetKey;
    internal static delegate* unmanaged[Cdecl]<int, int> GetKeyDown;
    internal static delegate* unmanaged[Cdecl]<int, int> GetKeyUp;
    internal static delegate* unmanaged[Cdecl]<int, int> GetMouseButton;
    internal static delegate* unmanaged[Cdecl]<int, int> GetMouseButtonDown;
    internal static delegate* unmanaged[Cdecl]<int, int> GetMouseButtonUp;
    internal static delegate* unmanaged[Cdecl]<NativeVector2> GetMousePosition;
    internal static delegate* unmanaged[Cdecl]<NativeVector2> GetMouseDelta;
    internal static delegate* unmanaged[Cdecl]<float> GetMouseWheel;
    internal static delegate* unmanaged[Cdecl]<int, int> GetGamepadButton;
    internal static delegate* unmanaged[Cdecl]<int, int> GetGamepadButtonDown;
    internal static delegate* unmanaged[Cdecl]<int> IsGamepadConnected;
    internal static delegate* unmanaged[Cdecl]<NativeVector2> GetLeftStick;
    internal static delegate* unmanaged[Cdecl]<NativeVector2> GetRightStick;
    internal static delegate* unmanaged[Cdecl]<float> GetLeftTrigger;
    internal static delegate* unmanaged[Cdecl]<float> GetRightTrigger;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> IsAlive;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, int, int> CopyName;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, byte*, void> SetName;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> GetActiveSelf;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int, void> SetActiveSelf;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, int> GetActiveInHierarchy;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity> GetParent;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity> GetFirstChild;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity> GetNextSibling;
    internal static delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity, void> SetParent;
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
        GetFixedDeltaTime = callbacks->getFixedDeltaTime;
        Log = callbacks->log;
        GetKey = callbacks->getKey;
        GetKeyDown = callbacks->getKeyDown;
        GetKeyUp = callbacks->getKeyUp;
        GetMouseButton = callbacks->getMouseButton;
        GetMouseButtonDown = callbacks->getMouseButtonDown;
        GetMouseButtonUp = callbacks->getMouseButtonUp;
        GetMousePosition = callbacks->getMousePosition;
        GetMouseDelta = callbacks->getMouseDelta;
        GetMouseWheel = callbacks->getMouseWheel;
        GetGamepadButton = callbacks->getGamepadButton;
        GetGamepadButtonDown = callbacks->getGamepadButtonDown;
        IsGamepadConnected = callbacks->isGamepadConnected;
        GetLeftStick = callbacks->getLeftStick;
        GetRightStick = callbacks->getRightStick;
        GetLeftTrigger = callbacks->getLeftTrigger;
        GetRightTrigger = callbacks->getRightTrigger;
        IsAlive = callbacks->isAlive;
        CopyName = callbacks->copyName;
        SetName = callbacks->setName;
        GetActiveSelf = callbacks->getActiveSelf;
        SetActiveSelf = callbacks->setActiveSelf;
        GetActiveInHierarchy = callbacks->getActiveInHierarchy;
        GetParent = callbacks->getParent;
        GetFirstChild = callbacks->getFirstChild;
        GetNextSibling = callbacks->getNextSibling;
        SetParent = callbacks->setParent;
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

    internal static float ReadFixedDeltaTime() {
        return GetFixedDeltaTime != null ? GetFixedDeltaTime() : 0.0f;
    }

    internal static void WriteLog(int level, string message) {
        if (Log == null) {
            return;
        }

        string safeMessage = message ?? string.Empty;
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(safeMessage) + 1];
        Encoding.UTF8.GetBytes(safeMessage, 0, safeMessage.Length, bytes, 0);
        fixed (byte* ptr = bytes) {
            Log(level, ptr);
        }
    }

    internal static bool ReadKey(int key) {
        return GetKey != null && GetKey(key) != 0;
    }

    internal static bool ReadKeyDown(int key) {
        return GetKeyDown != null && GetKeyDown(key) != 0;
    }

    internal static bool ReadKeyUp(int key) {
        return GetKeyUp != null && GetKeyUp(key) != 0;
    }

    internal static bool ReadMouseButton(int button) {
        return GetMouseButton != null && GetMouseButton(button) != 0;
    }

    internal static bool ReadMouseButtonDown(int button) {
        return GetMouseButtonDown != null && GetMouseButtonDown(button) != 0;
    }

    internal static bool ReadMouseButtonUp(int button) {
        return GetMouseButtonUp != null && GetMouseButtonUp(button) != 0;
    }

    internal static Vector2 ReadMousePosition() {
        return GetMousePosition != null ? GetMousePosition().ToVector2() : Vector2.zero;
    }

    internal static Vector2 ReadMouseDelta() {
        return GetMouseDelta != null ? GetMouseDelta().ToVector2() : Vector2.zero;
    }

    internal static float ReadMouseWheel() {
        return GetMouseWheel != null ? GetMouseWheel() : 0.0f;
    }

    internal static bool ReadGamepadButton(int button) {
        return GetGamepadButton != null && GetGamepadButton(button) != 0;
    }

    internal static bool ReadGamepadButtonDown(int button) {
        return GetGamepadButtonDown != null && GetGamepadButtonDown(button) != 0;
    }

    internal static bool ReadIsGamepadConnected() {
        return IsGamepadConnected != null && IsGamepadConnected() != 0;
    }

    internal static Vector2 ReadLeftStick() {
        return GetLeftStick != null ? GetLeftStick().ToVector2() : Vector2.zero;
    }

    internal static Vector2 ReadRightStick() {
        return GetRightStick != null ? GetRightStick().ToVector2() : Vector2.zero;
    }

    internal static float ReadLeftTrigger() {
        return GetLeftTrigger != null ? GetLeftTrigger() : 0.0f;
    }

    internal static float ReadRightTrigger() {
        return GetRightTrigger != null ? GetRightTrigger() : 0.0f;
    }

    internal static bool ReadIsAlive(NativeEntity entity) {
        return IsAlive != null && IsAlive(entity) != 0;
    }

    internal static string ReadName(NativeEntity entity) {
        if (CopyName == null) {
            return string.Empty;
        }

        byte* buffer = stackalloc byte[NameBufferSize];
        int length = CopyName(entity, buffer, NameBufferSize);
        return length <= 0 ? string.Empty : Encoding.UTF8.GetString(buffer, length);
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
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeApiTable {

    public delegate* unmanaged[Cdecl]<float> getDeltaTime;
    public delegate* unmanaged[Cdecl]<float> getFixedDeltaTime;
    public delegate* unmanaged[Cdecl]<int, byte*, void> log;
    public delegate* unmanaged[Cdecl]<int, int> getKey;
    public delegate* unmanaged[Cdecl]<int, int> getKeyDown;
    public delegate* unmanaged[Cdecl]<int, int> getKeyUp;
    public delegate* unmanaged[Cdecl]<int, int> getMouseButton;
    public delegate* unmanaged[Cdecl]<int, int> getMouseButtonDown;
    public delegate* unmanaged[Cdecl]<int, int> getMouseButtonUp;
    public delegate* unmanaged[Cdecl]<NativeVector2> getMousePosition;
    public delegate* unmanaged[Cdecl]<NativeVector2> getMouseDelta;
    public delegate* unmanaged[Cdecl]<float> getMouseWheel;
    public delegate* unmanaged[Cdecl]<int, int> getGamepadButton;
    public delegate* unmanaged[Cdecl]<int, int> getGamepadButtonDown;
    public delegate* unmanaged[Cdecl]<int> isGamepadConnected;
    public delegate* unmanaged[Cdecl]<NativeVector2> getLeftStick;
    public delegate* unmanaged[Cdecl]<NativeVector2> getRightStick;
    public delegate* unmanaged[Cdecl]<float> getLeftTrigger;
    public delegate* unmanaged[Cdecl]<float> getRightTrigger;
    public delegate* unmanaged[Cdecl]<NativeEntity, int> isAlive;
    public delegate* unmanaged[Cdecl]<NativeEntity, byte*, int, int> copyName;
    public delegate* unmanaged[Cdecl]<NativeEntity, byte*, void> setName;
    public delegate* unmanaged[Cdecl]<NativeEntity, int> getActiveSelf;
    public delegate* unmanaged[Cdecl]<NativeEntity, int, void> setActiveSelf;
    public delegate* unmanaged[Cdecl]<NativeEntity, int> getActiveInHierarchy;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity> getParent;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity> getFirstChild;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity> getNextSibling;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeEntity, void> setParent;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> getPosition;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3, void> setPosition;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> getLocalPosition;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3, void> setLocalPosition;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3> getLocalScale;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeVector3, void> setLocalScale;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeQuaternion> getLocalRotation;
    public delegate* unmanaged[Cdecl]<NativeEntity, NativeQuaternion, void> setLocalRotation;
}
