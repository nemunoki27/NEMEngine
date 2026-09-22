namespace NEMEngine;

// 所有者とAssemblyの終了を各サービスへ接続する
internal static class ScriptServiceLifetime {

    // 個別Scriptが所有する処理を終了する
    internal static void EndOwner(ScriptBehaviour owner) {

        Coroutines.StopAllForOwner(owner);
        Timers.CancelOwnedBy(owner);
        EventOwnerTracker.CancelOwnedBy(owner);
    }

    // Assembly終了時の購読と予約を解放する
    internal static void ResetForReload() {

        SceneManager.ResetForReload();
        Application.ResetForReload();
        InputActions.ResetForReload();
        Timers.ResetForReload();
        Coroutines.ResetForReload();
        EventBus.ResetForReload();
        EventOwnerTracker.ResetForReload();
        EventDispatch.ResetForReload();
    }

    // Nativeの更新phaseに対応する処理を進める
    internal static void TickFrame(int phase) {

        switch (phase) {
        case 0:
            EventDispatch.FlushDeferred();
            Timers.Tick();
            Coroutines.Tick(CoroutinePhase.Update);
            break;
        case 1:
            Coroutines.Tick(CoroutinePhase.Fixed);
            break;
        case 2:
            Coroutines.Tick(CoroutinePhase.EndOfFrame);
            break;
        }
    }
}
