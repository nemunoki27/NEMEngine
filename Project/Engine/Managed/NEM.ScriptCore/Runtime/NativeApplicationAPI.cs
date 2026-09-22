using System.Text;

namespace NEMEngine;

using static NEMEngine.NativeAPI;

// 接続済みcallbackを用途別に呼び出す
internal static unsafe class NativeApplicationAPI {

    internal static float ReadDeltaTime() {

        // ランタイム未初期化時はスクリプトを安全に動かさず0秒として扱う
        return GetDeltaTime != null ? GetDeltaTime() : 0.0f;
    }

    internal static float ReadEasedValue(int easingType, float t) {
        // ランタイム未初期化時は補間せずそのままのtを返す
        return EasedValue != null ? EasedValue(easingType, t) : t;
    }

    internal static float ReadFixedDeltaTime() {
        return GetFixedDeltaTime != null ? GetFixedDeltaTime() : 0.0f;
    }

    internal static void WriteLog(int level, string message) {
        if (Log == null) {
            return;
        }

        string safeMessage = message ?? string.Empty;
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(safeMessage) + 1];
        Encoding.UTF8.GetBytes(safeMessage, 0, safeMessage.Length, bytes, 0);
        fixed (byte* ptr = bytes) {
            Log(level, ptr);
        }
    }

    internal static void ReportScriptExceptionJson(string json) {
        if (ReportScriptException == null) {
            return;
        }

        string safe = json ?? string.Empty;
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(safe) + 1];
        Encoding.UTF8.GetBytes(safe, 0, safe.Length, bytes, 0);
        fixed (byte* ptr = bytes) {
            ReportScriptException(ptr);
        }
    }

    internal static float ReadUnscaledDeltaTime() => GetUnscaledDeltaTime != null ? GetUnscaledDeltaTime() : 0.0f;

    internal static float ReadUnscaledFixedDeltaTime() => GetUnscaledFixedDeltaTime != null ? GetUnscaledFixedDeltaTime() : 0.0f;

    internal static double ReadTimeSinceStartup() => GetTimeSinceStartup != null ? GetTimeSinceStartup() : 0.0;

    internal static double ReadUnscaledTime() => GetUnscaledTime != null ? GetUnscaledTime() : 0.0;

    internal static float ReadTimeScale() => GetTimeScale != null ? GetTimeScale() : 1.0f;

    internal static void WriteTimeScale(float value) { if (SetTimeScale != null) { SetTimeScale(value); } }

    internal static ulong ReadFrameCount() => GetFrameCount != null ? GetFrameCount() : 0ul;

    internal static bool ReadAssetExists(AssetGUID assetID) =>
        AssetExists != null && AssetExists(assetID) != 0;

    internal static bool ReadHasFocus() => GetHasFocus == null || GetHasFocus() != 0;

    internal static void RequestApplicationQuitCall() { if (RequestApplicationQuit != null) { RequestApplicationQuit(); } }

    internal static string ReadProjectRoot() {
        if (CopyProjectRoot == null) {
            return string.Empty;
        }
        int needed = CopyProjectRoot(null, 0);
        if (needed <= 0) {
            return string.Empty;
        }
        byte[] bytes = new byte[needed + 1];
        fixed (byte* ptr = bytes) {
            int written = CopyProjectRoot(ptr, needed + 1);
            return written <= 0 ? string.Empty : Encoding.UTF8.GetString(bytes, 0, written);
        }
    }

    internal static string ReadAssetDisplayName(AssetGUID assetID) {
        if (CopyAssetDisplayName == null || !assetID.isValid) {
            return string.Empty;
        }
        int needed = CopyAssetDisplayName(assetID, null, 0);
        if (needed <= 0) {
            return string.Empty;
        }
        byte[] bytes = new byte[needed + 1];
        fixed (byte* ptr = bytes) {
            int written = CopyAssetDisplayName(assetID, ptr, needed + 1);
            return written <= 0 ? string.Empty : Encoding.UTF8.GetString(bytes, 0, written);
        }
    }

    internal static string ReadUserSettingsRoot() {
        if (CopyUserSettingsRoot == null) {
            return string.Empty;
        }
        int needed = CopyUserSettingsRoot(null, 0);
        if (needed <= 0) {
            return string.Empty;
        }
        byte[] bytes = new byte[needed + 1];
        fixed (byte* ptr = bytes) {
            int written = CopyUserSettingsRoot(ptr, needed + 1);
            return written <= 0 ? string.Empty : Encoding.UTF8.GetString(bytes, 0, written);
        }
    }
}
