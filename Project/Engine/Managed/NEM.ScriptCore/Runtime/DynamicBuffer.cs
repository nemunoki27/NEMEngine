using System.Runtime.CompilerServices;

namespace NEMEngine;

// ネイティブECSへ格納できる固定レイアウトのBuffer要素
public interface IBufferElementData<TSelf>
    where TSelf : unmanaged, IBufferElementData<TSelf> {

    static abstract int componentTypeID { get; }
}

// GameObjectとBuffer種別だけを保持し、操作時に現在のECS格納先を解決する
public readonly unsafe struct DynamicBuffer<T>
    where T : unmanaged, IBufferElementData<T> {

    private const int OperationReplace = 0;
    private const int OperationAppend = 1;
    private const int OperationSetElement = 2;
    private const int OperationRemoveAt = 3;
    private const int OperationResize = 4;
    private const int OperationClear = 5;

    private readonly GameObject owner;
    private readonly ulong instanceID;

    internal DynamicBuffer(GameObject owner) {
        this.owner = owner;
        instanceID = NativeEntityAPI.ReadComponentInstanceID(owner.native, T.componentTypeID);
    }

    // 削除と再追加で別のBufferへ接続しない
    private bool MatchesInstance() => instanceID != 0 &&
        NativeEntityAPI.ReadComponentInstanceID(owner.native, T.componentTypeID) == instanceID;

    private NativeEntity nativeEntity {
        get {
            if (!MatchesInstance()) {
                throw new MissingReferenceException("DynamicBufferの参照先は破棄されています");
            }
            return owner.native;
        }
    }

    public bool IsCreated =>
        MatchesInstance() && ReadLength() >= 0;

    public int Count {
        get {
            int count = ReadLength();
            if (count < 0) {
                throw new InvalidOperationException(
                    "DynamicBuffer is not attached to the GameObject.");
            }
            return count;
        }
    }

    public T this[int index] {
        get {
            ValidateIndex(index);
            T value = default;
            int copied = NativeEntityAPI.CopyDynamicBuffer(
                nativeEntity, T.componentTypeID, sizeof(T),
                index, &value, 1);
            if (copied != 1) {
                throw new InvalidOperationException(
                    "DynamicBuffer element could not be read.");
            }
            return value;
        }
        set {
            ValidateIndex(index);
            T copy = value;
            if (!NativeEntityAPI.MutateDynamicBuffer(
                nativeEntity, T.componentTypeID, sizeof(T),
                OperationSetElement, index, &copy, 1)) {
                throw new InvalidOperationException(
                    "DynamicBuffer element could not be written.");
            }
        }
    }

    // 末尾へ1要素追加する
    public void Add(T value) {
        if (!NativeEntityAPI.MutateDynamicBuffer(
            nativeEntity, T.componentTypeID, sizeof(T),
            OperationAppend, 0, &value, 1)) {
            throw new InvalidOperationException(
                "DynamicBuffer element could not be added.");
        }
    }

    // 末尾へ連続要素を追加し、ABI呼び出し回数を1回に抑える
    public void AddRange(ReadOnlySpan<T> values) {
        if (values.IsEmpty) {
            return;
        }
        fixed (T* data = values) {
            if (!NativeEntityAPI.MutateDynamicBuffer(
                nativeEntity, T.componentTypeID, sizeof(T),
                OperationAppend, 0, data, values.Length)) {
                throw new InvalidOperationException(
                    "DynamicBuffer elements could not be added.");
            }
        }
    }

    // 要素列全体を置き換える
    public void SetAll(ReadOnlySpan<T> values) {
        fixed (T* data = values) {
            if (!NativeEntityAPI.MutateDynamicBuffer(
                nativeEntity, T.componentTypeID, sizeof(T),
                OperationReplace, 0, data, values.Length)) {
                throw new InvalidOperationException(
                    "DynamicBuffer could not be replaced.");
            }
        }
    }

    public void RemoveAt(int index) {
        ValidateIndex(index);
        if (!NativeEntityAPI.MutateDynamicBuffer(
            nativeEntity, T.componentTypeID, sizeof(T),
            OperationRemoveAt, index, null, 0)) {
            throw new InvalidOperationException(
                "DynamicBuffer element could not be removed.");
        }
    }

    public void Resize(int count) {
        if (count < 0) {
            throw new ArgumentOutOfRangeException(nameof(count));
        }
        if (!NativeEntityAPI.MutateDynamicBuffer(
            nativeEntity, T.componentTypeID, sizeof(T),
            OperationResize, 0, null, count)) {
            throw new InvalidOperationException(
                "DynamicBuffer could not be resized.");
        }
    }

    public void Clear() {
        if (!NativeEntityAPI.MutateDynamicBuffer(
            nativeEntity, T.componentTypeID, sizeof(T),
            OperationClear, 0, null, 0)) {
            throw new InvalidOperationException(
                "DynamicBuffer could not be cleared.");
        }
    }

    public int CopyTo(Span<T> destination, int sourceIndex = 0) {
        if (sourceIndex < 0) {
            throw new ArgumentOutOfRangeException(nameof(sourceIndex));
        }
        fixed (T* data = destination) {
            int copied = NativeEntityAPI.CopyDynamicBuffer(
                nativeEntity, T.componentTypeID, sizeof(T),
                sourceIndex, data, destination.Length);
            if (copied < 0) {
                throw new InvalidOperationException(
                    "DynamicBuffer could not be read.");
            }
            return copied;
        }
    }

    public T[] ToArray() {
        int count = Count;
        if (count == 0) {
            return Array.Empty<T>();
        }

        T[] result = new T[count];
        int copied = CopyTo(result);
        if (copied != result.Length) {
            Array.Resize(ref result, copied);
        }
        return result;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private int ReadLength() {
        return NativeEntityAPI.ReadDynamicBufferLength(
            nativeEntity, T.componentTypeID, sizeof(T));
    }

    private void ValidateIndex(int index) {
        int count = Count;
        if ((uint)index >= (uint)count) {
            throw new ArgumentOutOfRangeException(nameof(index));
        }
    }
}
