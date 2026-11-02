using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;

namespace NEMEngine.Distribution;

// 完全削除を許可せずWindowsのごみ箱へ移す
[SupportedOSPlatform("windows")]
internal static class WindowsRecycleBin {
    [Flags]
    private enum OperationFlags : uint {
        Silent = 0x4, NoConfirmation = 0x10, AllowUndo = 0x40, NoErrorUI = 0x400,
        RecycleOnDelete = 0x80000, EarlyFailure = 0x100000
    }

    private const uint RecycleIfPossible = 0x80;
    private const uint FileSystemPath = 0x80058000;
    private const int AbortResult = unchecked((int)0x80004004);

    internal static string Move(string directory, CancellationToken token = default) {
        string recycled = "";
        Exception? failure = null;
        var thread = new Thread(() => {
            try { recycled = MoveOnSTA(directory, token); }
            catch (Exception error) { failure = error; }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        thread.Join();
        if (failure != null) ExceptionDispatchInfo.Capture(failure).Throw();
        return recycled;
    }

    private static string MoveOnSTA(string directory, CancellationToken token) {
        token.ThrowIfCancellationRequested();
        var operation = (IFileOperation)Activator.CreateInstance(Type.GetTypeFromCLSID(
            new Guid("3AD05575-8857-4850-9277-11B85BDB8E09"), true)!)!;
        IShellItem? item = null;
        try {
            var iid = typeof(IShellItem).GUID;
            SHCreateItemFromParsingName(directory, IntPtr.Zero, ref iid, out item);
            // ごみ箱へ移せない場合は最初の失敗で中止する
            operation.SetOperationFlags(OperationFlags.RecycleOnDelete | OperationFlags.EarlyFailure | OperationFlags.NoErrorUI |
                OperationFlags.Silent | OperationFlags.NoConfirmation | OperationFlags.AllowUndo);
            var sink = new RecycleProgress(token);
            operation.DeleteItem(item, sink);
            try { operation.PerformOperations(); }
            catch when (sink.Failure != null) { ExceptionDispatchInfo.Capture(sink.Failure).Throw(); throw; }
            operation.GetAnyOperationsAborted(out bool aborted);
            if (sink.Failure != null) ExceptionDispatchInfo.Capture(sink.Failure).Throw();
            if (aborted) throw new OperationCanceledException("ごみ箱への移動を中止しました");
            if (sink.RecycledPath.Length == 0) throw new IOException("ごみ箱への移動を確認できませんでした");
            return sink.RecycledPath;
        } finally {
            if (item != null) Marshal.FinalReleaseComObject(item);
            Marshal.FinalReleaseComObject(operation);
        }
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode, PreserveSig = false)]
    private static extern void SHCreateItemFromParsingName(string path, IntPtr context, ref Guid iid,
        [MarshalAs(UnmanagedType.Interface)] out IShellItem item);

    [ComImport, Guid("43826D1E-E718-42EE-BC55-A1E261C37BFE"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IShellItem {
        void BindToHandler(IntPtr context, ref Guid handler, ref Guid iid, out IntPtr result);
        void GetParent(out IShellItem parent);
        void GetDisplayName(uint kind, out IntPtr name);
        void GetAttributes(uint mask, out uint attributes);
        void Compare(IShellItem other, uint hint, out int order);
    }

    [ComImport, Guid("947AAB5F-0A5C-4C13-B4D6-4BF7836FC9F8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IFileOperation {
        void Advise(IFileOperationProgressSink sink, out uint cookie);
        void Unadvise(uint cookie);
        void SetOperationFlags(OperationFlags flags);
        void SetProgressMessage([MarshalAs(UnmanagedType.LPWStr)] string message);
        void SetProgressDialog([MarshalAs(UnmanagedType.Interface)] object dialog);
        void SetProperties([MarshalAs(UnmanagedType.Interface)] object properties);
        void SetOwnerWindow(IntPtr owner);
        void ApplyPropertiesToItem(IShellItem item);
        void ApplyPropertiesToItems([MarshalAs(UnmanagedType.Interface)] object items);
        void RenameItem(IShellItem item, [MarshalAs(UnmanagedType.LPWStr)] string name, IFileOperationProgressSink sink);
        void RenameItems([MarshalAs(UnmanagedType.Interface)] object items, [MarshalAs(UnmanagedType.LPWStr)] string name);
        void MoveItem(IShellItem item, IShellItem destination, [MarshalAs(UnmanagedType.LPWStr)] string name, IFileOperationProgressSink sink);
        void MoveItems([MarshalAs(UnmanagedType.Interface)] object items, IShellItem destination);
        void CopyItem(IShellItem item, IShellItem destination, [MarshalAs(UnmanagedType.LPWStr)] string name, IFileOperationProgressSink sink);
        void CopyItems([MarshalAs(UnmanagedType.Interface)] object items, IShellItem destination);
        void DeleteItem(IShellItem item, IFileOperationProgressSink sink);
        void DeleteItems([MarshalAs(UnmanagedType.Interface)] object items);
        void NewItem(IShellItem destination, uint attributes, [MarshalAs(UnmanagedType.LPWStr)] string name,
            [MarshalAs(UnmanagedType.LPWStr)] string template, IFileOperationProgressSink sink);
        void PerformOperations();
        void GetAnyOperationsAborted([MarshalAs(UnmanagedType.Bool)] out bool aborted);
    }

    [ComVisible(true), Guid("04B0F1A7-9490-44BC-96E1-4296A31252E2"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IFileOperationProgressSink {
        [PreserveSig] int StartOperations();
        [PreserveSig] int FinishOperations(int result);
        [PreserveSig] int PreRenameItem(uint flags, IShellItem item, [MarshalAs(UnmanagedType.LPWStr)] string name);
        [PreserveSig] int PostRenameItem(uint flags, IShellItem item, [MarshalAs(UnmanagedType.LPWStr)] string name, int result, IShellItem? created);
        [PreserveSig] int PreMoveItem(uint flags, IShellItem item, IShellItem destination, [MarshalAs(UnmanagedType.LPWStr)] string name);
        [PreserveSig] int PostMoveItem(uint flags, IShellItem item, IShellItem destination, [MarshalAs(UnmanagedType.LPWStr)] string name, int result, IShellItem? created);
        [PreserveSig] int PreCopyItem(uint flags, IShellItem item, IShellItem destination, [MarshalAs(UnmanagedType.LPWStr)] string name);
        [PreserveSig] int PostCopyItem(uint flags, IShellItem item, IShellItem destination, [MarshalAs(UnmanagedType.LPWStr)] string name, int result, IShellItem? created);
        [PreserveSig] int PreDeleteItem(uint flags, IShellItem item);
        [PreserveSig] int PostDeleteItem(uint flags, IShellItem item, int result, IShellItem? created);
        [PreserveSig] int PreNewItem(uint flags, IShellItem destination, [MarshalAs(UnmanagedType.LPWStr)] string name);
        [PreserveSig] int PostNewItem(uint flags, IShellItem destination, [MarshalAs(UnmanagedType.LPWStr)] string name,
            [MarshalAs(UnmanagedType.LPWStr)] string template, uint attributes, int result, IShellItem? created);
        [PreserveSig] int UpdateProgress(uint total, uint done);
        [PreserveSig] int ResetTimer();
        [PreserveSig] int PauseTimer();
        [PreserveSig] int ResumeTimer();
    }

    [ComVisible(true), ClassInterface(ClassInterfaceType.None)]
    private sealed class RecycleProgress(CancellationToken token) : IFileOperationProgressSink {
        public string RecycledPath { get; private set; } = "";
        public Exception? Failure { get; private set; }

        public int PreDeleteItem(uint flags, IShellItem item) {
            if (token.IsCancellationRequested) Failure = new OperationCanceledException(token);
            else if ((flags & RecycleIfPossible) == 0) Failure = new IOException("ごみ箱へ移せないため削除を中止しました",
                new InvalidOperationException($"削除フラグ: 0x{flags:X8}"));
            return Failure == null ? 0 : AbortResult;
        }

        public int PostDeleteItem(uint flags, IShellItem item, int result, IShellItem? created) {
            if (result < 0) Failure ??= Marshal.GetExceptionForHR(result);
            if (created != null && result >= 0) {
                IntPtr name = IntPtr.Zero;
                try {
                    created.GetDisplayName(FileSystemPath, out name);
                    RecycledPath = Marshal.PtrToStringUni(name) ?? "";
                } catch (Exception error) { Failure ??= error; }
                finally { Marshal.FreeCoTaskMem(name); }
            }
            return 0;
        }

        public int StartOperations() => 0;
        public int FinishOperations(int result) { if (result < 0) Failure ??= Marshal.GetExceptionForHR(result); return 0; }
        public int PreRenameItem(uint flags, IShellItem item, string name) => 0;
        public int PostRenameItem(uint flags, IShellItem item, string name, int result, IShellItem? created) => 0;
        public int PreMoveItem(uint flags, IShellItem item, IShellItem destination, string name) => 0;
        public int PostMoveItem(uint flags, IShellItem item, IShellItem destination, string name, int result, IShellItem? created) => 0;
        public int PreCopyItem(uint flags, IShellItem item, IShellItem destination, string name) => 0;
        public int PostCopyItem(uint flags, IShellItem item, IShellItem destination, string name, int result, IShellItem? created) => 0;
        public int PreNewItem(uint flags, IShellItem destination, string name) => 0;
        public int PostNewItem(uint flags, IShellItem destination, string name, string template, uint attributes, int result, IShellItem? created) => 0;
        public int UpdateProgress(uint total, uint done) => 0;
        public int ResetTimer() => 0;
        public int PauseTimer() => 0;
        public int ResumeTimer() => 0;
    }
}
