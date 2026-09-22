using System.Diagnostics;

namespace NEMEngine;

public static class Debug {

    public static void Log(object? message) {
        NativeApplicationAPI.WriteLog(0, message?.ToString() ?? string.Empty);
    }

    public static void LogWarning(object? message) {
        NativeApplicationAPI.WriteLog(1, message?.ToString() ?? string.Empty);
    }

    public static void LogError(object? message) {
        NativeApplicationAPI.WriteLog(2, message?.ToString() ?? string.Empty);
    }

    // そのフレームだけGameViewへ線を描く(Unity互換の可視化、LineDrawへの糖衣)
    public static void DrawLine(Vector3 start, Vector3 end, Color4 color, float thickness = 1.0f) {
        LineDraw.DrawLine(start, end, color, thickness);
    }

    // originからdirectionの長さ分だけレイを描く
    public static void DrawRay(Vector3 origin, Vector3 direction, Color4 color, float thickness = 1.0f) {
        LineDraw.DrawLine(origin, origin + direction, color, thickness);
    }

    public static bool isDebuggerAttached => Debugger.IsAttached;

    public static void Break() {
        if (Debugger.IsAttached) {
            Debugger.Break();
        }
    }
}
