using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace NEMEngine;

public static unsafe partial class HostBridge {
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int CreateTimelineClip(NativeEntity owner, byte* typeName, byte* configuration, int length, ulong* output) {
        if (output == null || typeName == null || configuration == null || length < 0 || length > 16 * 1024 * 1024)
            return (int)ManagedStatus.InvalidArgument;
        *output = 0;
        try {
            string name = ManagedUTF8Transfer.PtrToString(typeName) ?? string.Empty;
            string json = System.Text.Encoding.UTF8.GetString(new ReadOnlySpan<byte>(configuration, length));
            ManagedStatus status = session.timelineClips.Create(owner, name, json, out ulong handle);
            *output = handle;
            return (int)status;
        } catch (Exception ex) {
            ScriptInvocationDiagnostics.ReportRuntimeServiceException(nameof(CreateTimelineClip), ex);
            return (int)ManagedStatus.ScriptException;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int EvaluateTimelineClip(ulong handle, NativeTimelineFrame* frame) {
        if (frame == null) return (int)ManagedStatus.InvalidArgument;
        return (int)session.timelineClips.Evaluate(handle, *frame);
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int UpdateTimelineClipConfiguration(ulong handle, byte* typeName, byte* configuration, int length) {
        if (typeName == null || configuration == null || length < 0 || length > 16 * 1024 * 1024)
            return (int)ManagedStatus.InvalidArgument;
        try {
            string name = ManagedUTF8Transfer.PtrToString(typeName) ?? string.Empty;
            string json = System.Text.Encoding.UTF8.GetString(new ReadOnlySpan<byte>(configuration, length));
            return (int)session.timelineClips.Reconfigure(handle, name, json);
        } catch (Exception ex) {
            ScriptInvocationDiagnostics.ReportRuntimeServiceException(nameof(UpdateTimelineClipConfiguration), ex);
            return (int)ManagedStatus.ScriptException;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int UpdateTimelineClipContext(ulong handle, NativeTimelineBindingUpdate* bindings, int bindingCount,
        NativeTimelineParameterUpdate* parameters, int parameterCount) {
        if (bindingCount < 0 || parameterCount < 0 || bindingCount > 65536 || parameterCount > 65536 ||
            (bindingCount > 0 && bindings == null) || (parameterCount > 0 && parameters == null))
            return (int)ManagedStatus.InvalidArgument;
        return (int)session.timelineClips.UpdateContext(handle,
            new ReadOnlySpan<NativeTimelineBindingUpdate>(bindings, bindingCount),
            new ReadOnlySpan<NativeTimelineParameterUpdate>(parameters, parameterCount));
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int ReleaseTimelineClip(ulong handle, int reason) {
        if (reason is < 0 or > 7) return (int)ManagedStatus.InvalidArgument;
        return (int)session.timelineClips.Release(handle, (TimelineExitReason)reason);
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int CopyTimelineClipTypes(byte* output, int capacity) {
        byte[] bytes = session.timelineClips.Schema;
        if (output == null || capacity == 0) return bytes.Length;
        if (capacity <= bytes.Length) return -1;
        bytes.CopyTo(new Span<byte>(output, capacity));
        output[bytes.Length] = 0;
        return bytes.Length;
    }
}
