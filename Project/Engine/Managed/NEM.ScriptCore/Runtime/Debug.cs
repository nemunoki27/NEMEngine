namespace NEMEngine;

public static class Debug {

    public static void Log(object? message) {
        NativeApi.WriteLog(0, message?.ToString() ?? string.Empty);
    }

    public static void LogWarning(object? message) {
        NativeApi.WriteLog(1, message?.ToString() ?? string.Empty);
    }

    public static void LogError(object? message) {
        NativeApi.WriteLog(2, message?.ToString() ?? string.Empty);
    }
}
