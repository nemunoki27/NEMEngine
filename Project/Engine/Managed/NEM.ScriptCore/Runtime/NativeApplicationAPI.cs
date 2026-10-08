namespace NEMEngine;

using static NEMEngine.NativeAPI;

// 接続済みcallbackを用途別に呼び出す
internal static unsafe class NativeApplicationAPI {

    // callbackを終えた地点で更新の中断を確認する
    internal static bool ReadUpdateInterrupted() => IsUpdateInterrupted != null && IsUpdateInterrupted() != 0;

    // 倍率適用後の差分時刻を返す
    internal static float ReadDeltaTime() {

        // ランタイム未初期化時はスクリプトを安全に動かさず0秒として扱う
        return GetDeltaTime != null ? GetDeltaTime() : 0.0f;
    }

    // 指定した補間曲線の値を返す
    internal static float ReadEasedValue(int easingType, float t) {

        // ランタイム未初期化時は補間せずそのままのtを返す
        return EasedValue != null ? EasedValue(easingType, t) : t;
    }

    // 倍率適用後の固定差分時刻を返す
    internal static float ReadFixedDeltaTime() {

        return GetFixedDeltaTime != null ? GetFixedDeltaTime() : 0.0f;
    }

    // ログをNativeへ渡す
    internal static void WriteLog(int level, string message) {

        if (Log == null) {
            return;
        }

        string safeMessage = message ?? string.Empty;
        byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(safeMessage);
        fixed (byte* ptr = bytes) {
            Log(level, ptr);
        }
    }

    // Script例外のJSONをNativeへ渡す
    internal static void ReportScriptExceptionJson(string json) {

        if (ReportScriptException == null) {
            return;
        }

        string safe = json ?? string.Empty;
        byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(safe);
        fixed (byte* ptr = bytes) {
            ReportScriptException(ptr);
        }
    }

    // 倍率適用前の差分時刻を返す
    internal static float ReadUnscaledDeltaTime() => GetUnscaledDeltaTime != null ? GetUnscaledDeltaTime() : 0.0f;

    // 倍率適用前の固定差分時刻を返す
    internal static float ReadUnscaledFixedDeltaTime() =>
        GetUnscaledFixedDeltaTime != null ? GetUnscaledFixedDeltaTime() : 0.0f;

    // 倍率適用後の経過時刻を返す
    internal static double ReadTimeSinceStartup() => GetTimeSinceStartup != null ? GetTimeSinceStartup() : 0.0;

    // 倍率適用前の経過時刻を返す
    internal static double ReadUnscaledTime() => GetUnscaledTime != null ? GetUnscaledTime() : 0.0;

    // 時間倍率を返す
    internal static float ReadTimeScale() => GetTimeScale != null ? GetTimeScale() : 1.0f;

    // 時間倍率を設定する
    internal static void WriteTimeScale(float value) {

        if (SetTimeScale != null) {
            SetTimeScale(value);
        }
    }

    // 実行フレーム数を返す
    internal static ulong ReadFrameCount() => GetFrameCount != null ? GetFrameCount() : 0ul;

    // Assetの存在を確認する
    internal static bool ReadAssetExists(AssetGUID assetID) =>
        AssetExists != null && AssetExists(assetID) != 0;

    // アプリのフォーカス状態を返す
    internal static bool ReadHasFocus() => GetHasFocus == null || GetHasFocus() != 0;

    // フレーム終端の終了を要求する
    internal static void RequestApplicationQuitCall() {

        if (RequestApplicationQuit != null) {
            RequestApplicationQuit();
        }
    }

    // Projectのルートを読む
    internal static string ReadProjectRoot() {

        if (CopyProjectRoot == null) {
            return string.Empty;
        }
        return ManagedUTF8Transfer.ReadString(CopyProjectRoot);
    }

    // Assetの表示名を読む
    internal static string ReadAssetDisplayName(AssetGUID assetID) {

        if (CopyAssetDisplayName == null || !assetID.isValid) {
            return string.Empty;
        }
        return ManagedUTF8Transfer.ReadString(CopyAssetDisplayName, assetID);
    }

    // ユーザー設定のルートを読む
    internal static string ReadUserSettingsRoot() {

        if (CopyUserSettingsRoot == null) {
            return string.Empty;
        }
        return ManagedUTF8Transfer.ReadString(CopyUserSettingsRoot);
    }
}
