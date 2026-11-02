using System.Runtime.InteropServices;

namespace NEMEngine.Distribution;

internal static partial class WindowActivation {
    public static void Focus(nint window) {
        if (window == 0) return;
        if (IsIconic(window)) ShowWindow(window, 9);
        SetForegroundWindow(window);
    }

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool ShowWindow(nint window, int command);
    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool SetForegroundWindow(nint window);
    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool IsIconic(nint window);
}
