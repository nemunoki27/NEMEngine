namespace NEMEngine;

//============================================================================
//	EventDispatch
//	イベント配信の共通実装。例外隔離つき Raise と、次フレーム頭(Update phase)で
//	ドレインする遅延キューを提供する。GameEvent / EventBus / 既存の静的イベントが共有する。
//============================================================================
// main thread 専用（BehaviorSystem の phase）。Timers/Coroutines と同じくロックは持たない。
internal static class EventDispatch {

    // 遅延発火の実行体。flush 時に「その時点の購読者」へ即時 Raise させる
    private static readonly List<Action> deferred = new();

    // handlers を購読順・例外隔離で呼ぶ。GetInvocationList のスナップショットで
    // Raise 中の購読追加/解除・再入が安全になる（既存の Application/SceneManager と同じ保証）
    internal static void Raise(Action? handlers, string tag) {
        if (handlers == null) {
            return;
        }
        foreach (Action handler in handlers.GetInvocationList()) {
            try {
                handler();
            }
            catch (Exception ex) {
                NativeApi.WriteLog(2, $"[{tag}] event handler threw\n{ex}");
            }
        }
    }

    internal static void Raise<T>(Action<T>? handlers, T arg, string tag) {
        if (handlers == null) {
            return;
        }
        foreach (Action<T> handler in handlers.GetInvocationList()) {
            try {
                handler(arg);
            }
            catch (Exception ex) {
                NativeApi.WriteLog(2, $"[{tag}] event handler threw\n{ex}");
            }
        }
    }

    // 遅延発火を積む。dispatch は flush 時に呼ばれ、その時点の購読者へ配信するため
    // キュー滞留中に owner が破棄されても stale な呼び出しにはならない
    internal static void Enqueue(Action dispatch) {
        deferred.Add(dispatch);
    }

    // 次フレーム頭(Update phase)で TickFrame から 1 回呼ぶ。
    // flush 中に積まれた分は次フレームへ繰り越す（同フレーム無限ループ防止）。
    internal static void FlushDeferred() {

        int count = deferred.Count;
        if (count == 0) {
            return;
        }
        for (int i = 0; i < count; ++i) {
            try {
                deferred[i]();
            }
            catch (Exception ex) {
                NativeApi.WriteLog(2, $"[EventDispatch] deferred dispatch threw\n{ex}");
            }
        }
        // 開始時の件数だけを前方から取り除く。flush 中に追加された分(index >= count)は残す
        deferred.RemoveRange(0, count);
    }

    // DLL unload / Play Stop で遅延キューを捨て、古い GameScripts assembly の delegate を手放す
    internal static void ResetForReload() {
        deferred.Clear();
    }
}
