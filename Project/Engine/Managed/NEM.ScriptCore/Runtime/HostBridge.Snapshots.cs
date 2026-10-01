using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace NEMEngine;

public static unsafe partial class HostBridge {

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetSavedSerializedStateSize(NativeScriptInstanceHandle handle, int* outSize) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(GetSavedSerializedStateSize), () => {
            if (outSize == null) { return ManagedStatus.InvalidArgument; }
            *outSize = 0;
            if (!instances.TryResolveSlot(handle, out MonoBehaviour script)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            ScriptInstanceSlot slot = instances.slots[(int)handle.index];
            slot.savedStateSnapshot = null;

            // 保存callbackと未知Fieldを含めて一度だけ取得
            byte[] snapshot = Encoding.UTF8.GetBytes(session.codec.BuildSavedStateJson(script));
            if (!instances.TryResolveSlot(handle, out MonoBehaviour current) || !ReferenceEquals(script, current)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            slot.savedStateSnapshot = snapshot;
            *outSize = snapshot.Length;
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CopySavedSerializedState(NativeScriptInstanceHandle handle, byte* buffer, int capacity, int* written) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(CopySavedSerializedState), () => {
            if (written == null) { return ManagedStatus.InvalidArgument; }
            *written = 0;
            if (!instances.TryResolveSlot(handle, out _)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            ScriptInstanceSlot slot = instances.slots[(int)handle.index];
            if (slot.savedStateSnapshot is not byte[] snapshot) {
                return ManagedStatus.InvalidArgument;
            }
            ManagedStatus status = ManagedUTF8Transfer.WriteUtf8Blob(snapshot, buffer, capacity, written);
            if (status == ManagedStatus.Ok) {
                slot.savedStateSnapshot = null;
            }
            return status;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetReloadSerializedStateSize(NativeScriptInstanceHandle handle, int* outSize) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(GetReloadSerializedStateSize), () => {
            if (outSize == null) { return ManagedStatus.InvalidArgument; }
            *outSize = 0;
            if (!instances.TryResolveSlot(handle, out MonoBehaviour script)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            ScriptInstanceSlot slot = instances.slots[(int)handle.index];
            slot.reloadStateSnapshot = Encoding.UTF8.GetBytes(session.codec.BuildReloadStateJson(script));
            *outSize = slot.reloadStateSnapshot.Length;
            return ManagedStatus.Ok;
        });
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CopyReloadSerializedState(NativeScriptInstanceHandle handle, byte* buffer, int capacity, int* written) {

        return (int)ScriptInvocationDiagnostics.Guard(nameof(CopyReloadSerializedState), () => {
            if (written == null) { return ManagedStatus.InvalidArgument; }
            *written = 0;
            if (!instances.TryResolveSlot(handle, out _)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            ScriptInstanceSlot slot = instances.slots[(int)handle.index];
            if (slot.reloadStateSnapshot is not byte[] snapshot) {
                return ManagedStatus.InvalidArgument;
            }
            ManagedStatus status = ManagedUTF8Transfer.WriteUtf8Blob(snapshot, buffer, capacity, written);
            if (status == ManagedStatus.Ok) {
                slot.reloadStateSnapshot = null;
            }
            return status;
        });
    }

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
}
