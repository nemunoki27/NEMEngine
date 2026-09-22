using System.Runtime.InteropServices;

using System.Text;





namespace NEMEngine;

// Nativeとの文字列転送を行う
internal static unsafe class ManagedUTF8Transfer {

    internal static ManagedStatus WriteUtf8Blob(string text, byte* buffer, int capacity, int* written) {

        return WriteUtf8Blob(Encoding.UTF8.GetBytes(text), buffer, capacity, written);
    }

    internal static ManagedStatus WriteUtf8Blob(byte[] bytes, byte* buffer, int capacity, int* written) {

        if (written != null) {
            *written = bytes.Length;
        }
        if (buffer == null || capacity < bytes.Length) {
            return ManagedStatus.BufferTooSmall;
        }
        for (int i = 0; i < bytes.Length; ++i) {
            buffer[i] = bytes[i];
        }
        return ManagedStatus.Ok;
    }

    internal static string? PtrToString(byte* ptr) {

        // C++側のUTF-8 null終端文字列をC#文字列へ変換する
        return ptr == null ? null : Marshal.PtrToStringUTF8((IntPtr)ptr);
    }

    internal static void CopyFixed(string value, byte* buffer, int capacity) {

        // C++側 ManagedScriptTypeDescriptor の固定長バッファへ UTF-8 でコピーし null 終端する
        byte[] bytes = Encoding.UTF8.GetBytes(value);

        // 末尾null用に1byte空ける
        int length = Math.Min(bytes.Length, capacity - 1);
        for (int i = 0; i < length; ++i) {
            buffer[i] = bytes[i];
        }
        buffer[length] = 0;
    }
}
