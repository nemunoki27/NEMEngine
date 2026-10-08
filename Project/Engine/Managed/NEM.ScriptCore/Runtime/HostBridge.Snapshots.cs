using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace NEMEngine;

public static unsafe partial class HostBridge {

    // 保存値を確定して必要容量を返す
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetSavedSerializedStateSize(NativeScriptInstanceHandle handle, int* outSize) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(GetSavedSerializedStateSize),
            () => CaptureSerializedSnapshot(handle, outSize, false));
    }

    // 確定した保存値をコピーする
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CopySavedSerializedState(NativeScriptInstanceHandle handle, byte* buffer, int capacity, int* written) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(CopySavedSerializedState),
            () => CopySerializedSnapshot(handle, buffer, capacity, written, false));
    }

    // 再読込用の値を確定して必要容量を返す
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetReloadSerializedStateSize(NativeScriptInstanceHandle handle, int* outSize) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(GetReloadSerializedStateSize),
            () => CaptureSerializedSnapshot(handle, outSize, true));
    }

    // 確定した再読込用の値をコピーする
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CopyReloadSerializedState(NativeScriptInstanceHandle handle, byte* buffer, int capacity, int* written) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(CopyReloadSerializedState),
            () => CopySerializedSnapshot(handle, buffer, capacity, written, true));
    }

    // 再読込用の保存値を個体へ適用する
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int ApplyReloadSerializedState(NativeScriptInstanceHandle handle, byte* stateJson) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(ApplyReloadSerializedState), () => {
            if (!instances.TryResolveSlot(handle, out MonoBehaviour script)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            session.codec.ApplyReloadFields(script, ManagedUTF8Transfer.PtrToString(stateJson));
            return ManagedStatus.Ok;
        });
    }

    // 実行値を確定して必要容量を返す
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetRuntimeSerializedStateSize(NativeScriptInstanceHandle handle, int* outSize) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(GetRuntimeSerializedStateSize), () => {
            if (outSize == null) {
                return ManagedStatus.InvalidArgument;
            }
            *outSize = 0;
            ManagedStatus status = CaptureRuntimeSnapshot(handle, true, out byte[]? snapshot);
            if (status == ManagedStatus.Ok) {
                *outSize = snapshot!.Length;
            }
            return status;
        });
    }

    // 確定した実行値をコピーする
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CopyRuntimeSerializedState(NativeScriptInstanceHandle handle, byte* buffer, int capacity, int* written) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(CopyRuntimeSerializedState), () => {
            if (written == null) {
                return ManagedStatus.InvalidArgument;
            }
            *written = 0;
            ManagedStatus status = CaptureRuntimeSnapshot(handle, false, out byte[]? snapshot);
            if (status != ManagedStatus.Ok) {
                return status;
            }
            status = ManagedUTF8Transfer.WriteUtf8Blob(snapshot!, buffer, capacity, written);
            if (status == ManagedStatus.Ok) {
                instances.slots[(int)handle.index].runtimeStateSnapshot = null;
            }
            return status;
        });
    }

    // 保存callback後も同じScriptなら確定したデータを公開する
    private static ManagedStatus CaptureSerializedSnapshot(NativeScriptInstanceHandle handle, int* outSize, bool reload) {

        if (outSize == null) {
            return ManagedStatus.InvalidArgument;
        }
        *outSize = 0;
        if (!instances.TryResolveSlot(handle, out MonoBehaviour script)) {
            return ManagedStatus.InvalidInstanceHandle;
        }
        ScriptInstanceSlot slot = instances.slots[(int)handle.index];
        if (reload) {
            slot.reloadStateSnapshot = null;
        } else {
            slot.savedStateSnapshot = null;
        }

        // ユーザー処理中の解放とslot再利用を確認する
        byte[] snapshot = Encoding.UTF8.GetBytes(reload
            ? session.codec.BuildReloadStateJSON(script) : session.codec.BuildSavedStateJSON(script));
        if (!instances.TryResolveSlot(handle, out MonoBehaviour current) || !ReferenceEquals(script, current)) {
            return ManagedStatus.InvalidInstanceHandle;
        }
        if (reload) {
            slot.reloadStateSnapshot = snapshot;
        } else {
            slot.savedStateSnapshot = snapshot;
        }
        *outSize = snapshot.Length;
        return ManagedStatus.Ok;
    }

    // 参照解決callback後も同じScriptなら実行値を公開する
    private static ManagedStatus CaptureRuntimeSnapshot(NativeScriptInstanceHandle handle, bool refresh, out byte[]? snapshot) {

        snapshot = null;
        if (!instances.TryResolveSlot(handle, out MonoBehaviour script)) {
            return ManagedStatus.InvalidInstanceHandle;
        }
        ScriptInstanceSlot slot = instances.slots[(int)handle.index];
        if (!refresh && slot.runtimeStateSnapshot is byte[] cached) {
            snapshot = cached;
            return ManagedStatus.Ok;
        }
        // 旧データを解除してから参照解決と取得を行う
        slot.runtimeStateSnapshot = null;
        byte[] captured = Encoding.UTF8.GetBytes(session.codec.BuildRuntimeStateJSON(script));
        if (!instances.TryResolveSlot(handle, out MonoBehaviour current) || !ReferenceEquals(script, current)) {
            return ManagedStatus.InvalidInstanceHandle;
        }
        slot.runtimeStateSnapshot = captured;
        snapshot = captured;
        return ManagedStatus.Ok;
    }

    // 確定した保存値はコピー成功後にだけ手放す
    private static ManagedStatus CopySerializedSnapshot(NativeScriptInstanceHandle handle,
        byte* buffer, int capacity, int* written, bool reload) {

        if (written == null) {
            return ManagedStatus.InvalidArgument;
        }
        *written = 0;
        if (!instances.TryResolveSlot(handle, out _)) {
            return ManagedStatus.InvalidInstanceHandle;
        }
        ScriptInstanceSlot slot = instances.slots[(int)handle.index];
        byte[]? snapshot = reload ? slot.reloadStateSnapshot : slot.savedStateSnapshot;
        if (snapshot == null) {
            return ManagedStatus.InvalidArgument;
        }

        // 容量不足時は同じ確定値で再試行する
        ManagedStatus status = ManagedUTF8Transfer.WriteUtf8Blob(snapshot, buffer, capacity, written);
        if (status == ManagedStatus.Ok) {
            if (reload) {
                slot.reloadStateSnapshot = null;
            } else {
                slot.savedStateSnapshot = null;
            }
        }
        return status;
    }
}
