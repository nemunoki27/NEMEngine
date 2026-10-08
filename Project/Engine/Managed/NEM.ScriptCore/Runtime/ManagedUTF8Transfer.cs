using System.Buffers;
using System.Runtime.InteropServices;
using System.Text;

namespace NEMEngine;

// Nativeとの文字列転送を行う
internal static unsafe class ManagedUTF8Transfer {

    // null終端を含む転送容量を求める
    internal static int GetNullTerminatedCapacity(string value) {

        return checked(Encoding.UTF8.GetByteCount(value) + 1);
    }

    // 末尾を空けてUTF-8を書き込む
    internal static void WriteNullTerminated(string value, Span<byte> destination) {

        if (destination.IsEmpty) {
            throw new ArgumentException("Native string buffer is empty.", nameof(destination));
        }
        int length = Encoding.UTF8.GetBytes(value, destination[..^1]);
        destination[length] = 0;
    }

    // 必要な容量でnull終端文字列を作る
    internal static byte[] GetNullTerminatedBytes(string value) {

        byte[] bytes = new byte[GetNullTerminatedCapacity(value)];
        WriteNullTerminated(value, bytes);
        return bytes;
    }

    // 短い名前はstack、長い名前は必要な容量で受け取る
    internal static string ReadEntityString(delegate* unmanaged[Cdecl]<NativeEntity, byte*, int, int> copy,
        NativeEntity entity) {

        byte* shortBuffer = stackalloc byte[StackCapacity];
        int length = copy(entity, shortBuffer, StackCapacity);
        if (length < 0 || length >= StackCapacity) {
            throw new InvalidOperationException("Invalid Native string length.");
        }
        if (length < StackCapacity - 1) {
            return Encoding.UTF8.GetString(shortBuffer, length);
        }
        return ReadString(new EntityStringSource(copy, entity));
    }

    // 引数のないNative文字列を読む
    internal static string ReadString(delegate* unmanaged[Cdecl]<byte*, int, int> copy) {

        return ReadString(new GlobalStringSource(copy));
    }

    // Assetを指定してNative文字列を読む
    internal static string ReadString(delegate* unmanaged[Cdecl]<AssetGUID, byte*, int, int> copy, AssetGUID assetID) {

        return ReadString(new AssetStringSource(copy, assetID));
    }

    // Entityを指定してNative文字列を読む
    internal static string ReadString(delegate* unmanaged[Cdecl]<NativeEntity, byte*, int, int> copy, NativeEntity entity) {

        return ReadString(new EntityStringSource(copy, entity));
    }

    // UTF-8へ変換して二段階転送
    internal static ManagedStatus WriteUtf8Blob(string text, byte* buffer, int capacity, int* written) {

        return WriteUtf8Blob(Encoding.UTF8.GetBytes(text), buffer, capacity, written);
    }

    // 生成済みUTF-8を必要容量付きで転送
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

    // Nativeのnull終端文字列を取得
    internal static string? PtrToString(byte* ptr) {

        // C++側のUTF-8 null終端文字列をC#文字列へ変換する
        return ptr == null ? null : Marshal.PtrToStringUTF8((IntPtr)ptr);
    }

    // 固定長の末尾を空けて名前を転送
    internal static void CopyFixed(string value, byte* buffer, int capacity) {

        // 固定長バッファへUTF-8をコピーする
        if (capacity <= 0) {
            return;
        }
        byte[] bytes = Encoding.UTF8.GetBytes(value);

        // 末尾null用に1byte空ける
        int length = Mathf.Min(bytes.Length, capacity - 1);
        // UTF-8の途中で固定長文字列を切らない
        while (length > 0 && length < bytes.Length && (bytes[length] & 0xC0) == 0x80) {
            --length;
        }
        for (int i = 0; i < length; ++i) {
            buffer[i] = bytes[i];
        }
        buffer[length] = 0;
    }

    // 文字列取得の引数だけを用途別に保持する
    private interface IStringSource {
        int Copy(byte* buffer, int capacity);
    }

    // 引数のない文字列取得
    private readonly struct GlobalStringSource : IStringSource {

        public GlobalStringSource(delegate* unmanaged[Cdecl]<byte*, int, int> copy) { this.copy = copy; }

        public int Copy(byte* buffer, int capacity) => copy(buffer, capacity);

        private readonly delegate* unmanaged[Cdecl]<byte*, int, int> copy;
    }

    // Assetを指定する文字列取得
    private readonly struct AssetStringSource : IStringSource {

        public AssetStringSource(delegate* unmanaged[Cdecl]<AssetGUID, byte*, int, int> copy, AssetGUID assetID) {
            this.copy = copy;
            this.assetID = assetID;
        }

        public int Copy(byte* buffer, int capacity) => copy(assetID, buffer, capacity);

        private readonly delegate* unmanaged[Cdecl]<AssetGUID, byte*, int, int> copy;
        private readonly AssetGUID assetID;
    }

    // Entityを指定する文字列取得
    private readonly struct EntityStringSource : IStringSource {

        public EntityStringSource(delegate* unmanaged[Cdecl]<NativeEntity, byte*, int, int> copy, NativeEntity entity) {
            this.copy = copy;
            this.entity = entity;
        }

        public int Copy(byte* buffer, int capacity) => copy(entity, buffer, capacity);

        private readonly delegate* unmanaged[Cdecl]<NativeEntity, byte*, int, int> copy;
        private readonly NativeEntity entity;
    }

    private const int StackCapacity = 256;
    private const int MaxAttempts = 3;

    // 長さが変わった文字列は容量を更新して取得する
    private static string ReadString<TSource>(TSource source) where TSource : struct, IStringSource {

        int required = source.Copy(null, 0);
        byte* shortBuffer = stackalloc byte[StackCapacity];
        for (int attempt = 0; attempt < MaxAttempts; ++attempt) {
            if (required < 0) {
                throw new InvalidOperationException("Invalid Native string capacity.");
            }
            if (required == 0) {
                return string.Empty;
            }

            // 短い文字列はstackで受け取る
            int capacity = checked(required + 1);
            byte[]? bytes = capacity > StackCapacity ? ArrayPool<byte>.Shared.Rent(capacity) : null;
            try {
                fixed (byte* rented = bytes) {
                    byte* buffer = bytes is null ? shortBuffer : rented;
                    int length = source.Copy(buffer, capacity);
                    if (length < 0 || length >= capacity) {
                        throw new InvalidOperationException("Invalid Native string length.");
                    }

                    // 容量の末尾まで書かれた場合だけ長さを照合する
                    int currentLength = length == capacity - 1 ? source.Copy(null, 0) : length;
                    if (currentLength < 0) {
                        throw new InvalidOperationException("Invalid Native string capacity.");
                    }
                    if (currentLength <= length) {
                        return Encoding.UTF8.GetString(buffer, length);
                    }
                    required = currentLength;
                }
            }
            finally {
                if (bytes is not null) {
                    ArrayPool<byte>.Shared.Return(bytes);
                }
            }
        }
        throw new InvalidOperationException("Native string changed during transfer.");
    }
}
