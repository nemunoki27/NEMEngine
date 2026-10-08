namespace NEMEngine;

// Controllerの型付きParameterをNativeへ渡す
internal static unsafe class NativeAnimatorAPI {

    internal static void SetParameter(NativeEntity entity, string name, int type, float number, int integer) {

        ArgumentException.ThrowIfNullOrEmpty(name);
        if (NativeAPI.SetAnimatorParameter == null) {
            return;
        }
        byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(name ?? string.Empty);
        fixed (byte* pointer = bytes) {
            if (NativeAPI.SetAnimatorParameter(entity, pointer, type, number, integer) == 0) {
                Debug.LogWarning($"Controllerに指定型のParameterがありません: {name}");
            }
        }
    }

    internal static void GetParameter(NativeEntity entity, string name, int type, out float number, out int integer) {

        ArgumentException.ThrowIfNullOrEmpty(name);
        float floatValue = 0.0f;
        int integerValue = 0;
        if (NativeAPI.GetAnimatorParameter != null) {

            byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(name ?? string.Empty);
            fixed (byte* pointer = bytes) {
                if (NativeAPI.GetAnimatorParameter(entity, pointer, type, &floatValue, &integerValue) == 0) {
                    Debug.LogWarning($"Controllerに指定型のParameterがありません: {name}");
                }
            }
        }
        number = floatValue;
        integer = integerValue;
    }
}
