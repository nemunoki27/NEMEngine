using System.Runtime.InteropServices;
using System.Buffers;

using System.Text;





namespace NEMEngine;

// Nativeとの文字列転送を行う
internal static unsafe class ManagedUTF8Transfer {

    // 短い名前はstack、長い名前は必要な容量で受け取る
    internal static string ReadEntityString(delegate* unmanaged[Cdecl]<NativeEntity, byte*, int, int> copy, NativeEntity entity) {
        const int stackCapacity = 256;
        byte* shortBuffer = stackalloc byte[stackCapacity];
        int length = copy(entity, shortBuffer, stackCapacity);
        if (length < 0 || length >= stackCapacity) { throw new InvalidOperationException("Invalid Native string length."); }
        if (length < stackCapacity - 1) { return Encoding.UTF8.GetString(shortBuffer, length); }
        int required = copy(entity, null, 0);
        if (required < 0) { throw new InvalidOperationException("Invalid Native string capacity."); }
        byte[] bytes = ArrayPool<byte>.Shared.Rent(checked(required + 1));
        try {
            fixed (byte* buffer = bytes) {
                length = copy(entity, buffer, bytes.Length);
                if (length < 0 || length >= bytes.Length) { throw new InvalidOperationException("Invalid Native string length."); }
                return Encoding.UTF8.GetString(buffer, length);
            }
        }
        finally { ArrayPool<byte>.Shared.Return(bytes); }
    }

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
        if (capacity <= 0) { return; }
        byte[] bytes = Encoding.UTF8.GetBytes(value);

        // 末尾null用に1byte空ける
        int length = Mathf.Min(bytes.Length, capacity - 1);
        // UTF-8の途中で固定長文字列を切らない
        while (length > 0 && length < bytes.Length && (bytes[length] & 0xC0) == 0x80) { --length; }
        for (int i = 0; i < length; ++i) {
            buffer[i] = bytes[i];
        }
        buffer[length] = 0;
    }
}
