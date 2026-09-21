namespace NEMEngine;

// アプリケーションの lifecycle イベント。state は native の frame snapshot / time service を読む。
// イベントは BehaviorSystem の Pass4 で pump され、DLL reload 前に自動解除される（ResetForReload）。
// native pointer は payload に含めない。
public static class Application {

    private static bool prevFocus = true;
    private static bool prevPaused;
    private static bool initialized;

    // ウィンドウがフォーカスを持っているか（Win32 WM_SETFOCUS/KILLFOCUS/ACTIVATE 由来）
    public static bool HasFocus => NativeAPI.ReadHasFocus();
    // runtime pause 状態。既存の application pause state が無いため TimeScale==0 を pause とみなす。
    public static bool IsPaused => NativeAPI.ReadTimeScale() == 0.0f;

    // 実行中プロジェクトの絶対パスを返す、製品ビルドでは製品フォルダーを指す
    public static string ProjectRoot => NativeAPI.ReadProjectRoot();

    public static event Action<bool>? FocusChanged;
    public static event Action<bool>? PauseChanged;
    public static event Action? Quitting;

    // DebugとDevelopではPlayを終了し、Releaseではアプリケーションを終了する。
    public static void Quit() {
        NativeAPI.RequestApplicationQuitCall();
    }

    // 毎フレーム（Pass4）pump され、focus / pause 状態の変化を検出して通知する。
    internal static void PumpEvents() {
        bool focus = HasFocus;
        bool paused = IsPaused;
        if (!initialized) {
            // 初回は基準値だけ取って通知しない
            prevFocus = focus;
            prevPaused = paused;
            initialized = true;
            return;
        }
        if (focus != prevFocus) {
            prevFocus = focus;
            EventDispatch.Raise(FocusChanged, focus, "Application");
        }
        if (paused != prevPaused) {
            prevPaused = paused;
            EventDispatch.Raise(PauseChanged, paused, "Application");
        }
    }

    // application shutdown 前に native から一度だけ呼ばれる。
    internal static void RaiseQuitting() {
        EventDispatch.Raise(Quitting, "Application");
    }

    // DLL reload(unload) 前に呼ばれ、古い assembly の delegate を手放す。
    internal static void ResetForReload() {
        FocusChanged = null;
        PauseChanged = null;
        Quitting = null;
        initialized = false;
    }
}
