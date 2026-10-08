using System.Diagnostics;
using System.Runtime.InteropServices;

namespace NEMEngine;

// 指定された場合だけデバッガ接続を待つ
internal static unsafe class ManagedDebuggerWait {

    // 指定された接続待機を開始
    internal static void WaitForManagedDebuggerIfRequested() {

        // 環境変数でC#デバッガの接続待機を選択
        string? wait = Environment.GetEnvironmentVariable("NEM_MANAGED_WAIT_FOR_DEBUGGER");
        if (wait != "1" || Debugger.IsAttached) {
            return;
        }

        if (IsDebuggerPresent()) {
            NativeApplicationAPI.WriteLog(1,
                "Managed debugger wait skipped: process is already debugged. " +
                "If you want C# breakpoints in GameScripts Visual Studio, run Sandbox without native C++ debugging and then Attach to Process.");
            return;
        }

        // 環境変数から待機時間を取得
        int timeoutMS = 15000;
        string? timeoutText = Environment.GetEnvironmentVariable("NEM_MANAGED_WAIT_TIMEOUT_MS");
        if (!string.IsNullOrWhiteSpace(timeoutText) &&
            int.TryParse(timeoutText, out int parsedTimeout) &&
            0 < parsedTimeout) {
            timeoutMS = parsedTimeout;
        }

        NativeApplicationAPI.WriteLog(0, $"Waiting for managed debugger attach... timeout={timeoutMS}ms");

        // 接続かタイムアウトまで待機
        Stopwatch stopwatch = Stopwatch.StartNew();
        while (!Debugger.IsAttached && stopwatch.ElapsedMilliseconds < timeoutMS) {
            Thread.Sleep(100);
        }

        if (Debugger.IsAttached) {
            NativeApplicationAPI.WriteLog(0, "Managed debugger attached.");
        } else {
            NativeApplicationAPI.WriteLog(1, "Managed debugger was not attached before timeout. Continue execution.");
        }
    }

    // Nativeデバッガの接続を確認
    [DllImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsDebuggerPresent();
}
