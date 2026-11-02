using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Runtime.Versioning;

namespace NEMEngine.Distribution;

// 起動済みSolutionの検索をランチャーとIDE連携で共有する
[SupportedOSPlatform("windows")]
public static class VisualStudioInstances {
    [DllImport("ole32.dll", PreserveSig = false)]
    private static extern void GetRunningObjectTable(int reserved, out IRunningObjectTable table);
    [DllImport("ole32.dll", PreserveSig = false)]
    private static extern void CreateBindCtx(int reserved, out IBindCtx context);

    public static bool IsOpen(string solution) {
        bool opened = false;
        Exception? failure = null;
        var thread = new Thread(() => {
            try {
                object? instance = Find(solution);
                opened = instance != null;
                if (instance != null) Marshal.ReleaseComObject(instance);
            } catch (Exception error) { failure = error; }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        thread.Join();
        if (failure != null) ExceptionDispatchInfo.Capture(failure).Throw();
        return opened;
    }

    public static object? Find(string solution) {
        solution = Path.GetFullPath(solution);
        GetRunningObjectTable(0, out var table);
        CreateBindCtx(0, out var context);
        table.EnumRunning(out var enumerator);
        var monikers = new IMoniker[1];
        try {
            while (enumerator.Next(1, monikers, 0) == 0) {
                object? candidate = null;
                try {
                    monikers[0].GetDisplayName(context, null, out string name);
                    if (!name.StartsWith("!VisualStudio.DTE.", StringComparison.Ordinal)) continue;
                    table.GetObject(monikers[0], out candidate);
                    dynamic dte = candidate;
                    string path = dte.Solution.FullName;
                    if (!string.IsNullOrEmpty(path) && Path.GetFullPath(path).Equals(solution, StringComparison.OrdinalIgnoreCase)) {
                        object result = candidate;
                        candidate = null;
                        return result;
                    }
                } catch (Exception error) when (error is COMException or Microsoft.CSharp.RuntimeBinder.RuntimeBinderException) { }
                finally {
                    if (candidate != null) Marshal.ReleaseComObject(candidate);
                    Marshal.ReleaseComObject(monikers[0]);
                }
            }
        } finally {
            Marshal.ReleaseComObject(enumerator);
            Marshal.ReleaseComObject(context);
            Marshal.ReleaseComObject(table);
        }
        return null;
    }
}
