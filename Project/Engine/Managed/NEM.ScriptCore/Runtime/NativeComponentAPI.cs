using System.Text;

namespace NEMEngine;

using static NEMEngine.NativeAPI;

// Componentの値転送とNativeの結果確認
internal static unsafe class NativeComponentAPI {

    // Componentの値を取得する
    internal static void ComponentGet(NativeEntity entity, int typeID, int propertyID, void* outValue, int valueSize) {

        if (GetComponentProperty == null) {
            throw new NotSupportedException("Component getter is not connected.");
        }
        RequireComponentStatus(GetComponentProperty(entity, typeID, propertyID, outValue, valueSize));
    }

    // Componentの値を設定する
    internal static void ComponentSet(NativeEntity entity, int typeID, int propertyID, void* value, int valueSize) {

        if (SetComponentProperty == null) {
            throw new NotSupportedException("Component setter is not connected.");
        }
        RequireComponentStatus(SetComponentProperty(entity, typeID, propertyID, value, valueSize));
    }

    // 必要なサイズを取得して文字列を読む
    internal static string ComponentGetString(NativeEntity entity, int typeID, int propertyID) {

        if (GetComponentStringProperty == null) {
            throw new NotSupportedException("Component string getter is not connected.");
        }
        // 必要な文字列サイズを問い合わせる
        int needed = 0;
        int status = GetComponentStringProperty(entity, typeID, propertyID, null, 0, &needed);
        if (status != (int)ManagedStatus.BufferTooSmall) {
            RequireComponentStatus(status);
        }
        if (needed < 0) {
            throw new InvalidOperationException("Invalid component string size.");
        }
        if (needed <= 0) {
            return string.Empty;
        }
        byte[] bytes = new byte[needed];
        int written = 0;
        fixed (byte* ptr = bytes) {
            RequireComponentStatus(GetComponentStringProperty(entity, typeID, propertyID, ptr, needed, &written));
            if (written < 0 || written > needed) {
                throw new InvalidOperationException("Invalid component string length.");
            }
        }
        return written <= 0 ? string.Empty : Encoding.UTF8.GetString(bytes, 0, written);
    }

    // Componentの文字列を設定する
    internal static void ComponentSetString(NativeEntity entity, int typeID, int propertyID, string value) {

        if (SetComponentStringProperty == null) {
            throw new NotSupportedException("Component string setter is not connected.");
        }
        string safe = value ?? string.Empty;
        byte[] bytes = Encoding.UTF8.GetBytes(safe);
        fixed (byte* ptr = bytes) {
            RequireComponentStatus(SetComponentStringProperty(entity, typeID, propertyID, ptr, bytes.Length));
        }
    }

    // Native側の失敗を値の取得成功として扱わない
    private static void RequireComponentStatus(int result) {

        ManagedStatus status = (ManagedStatus)result;
        if (status == ManagedStatus.Ok) {
            return;
        }
        if (status is ManagedStatus.InvalidEntityHandle or ManagedStatus.InvalidWorldHandle or
            ManagedStatus.InvalidInstanceHandle) {
            throw new MissingReferenceException($"Component access failed: {status}");
        }
        throw new InvalidOperationException($"Component access failed: {status}");
    }
}
