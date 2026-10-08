namespace NEMEngine;

//============================================================================
//	EventDispatch
//	イベント通知と次回更新用の配信待ちを共有する
//============================================================================
// ゲーム更新のメインスレッドから使用する
internal static class EventDispatch {

    // 次回更新で通知するイベント
    private static readonly List<Action> deferred = new();

    // 購読順に通知し、例外は呼出元へ返す
    internal static void Raise(Action? handlers, string tag) {
        if (handlers == null || NativeApplicationAPI.ReadUpdateInterrupted()) {
            return;
        }
        foreach (Action handler in handlers.GetInvocationList()) {
            try {
                handler();
                if (NativeApplicationAPI.ReadUpdateInterrupted()) { return; }
            }
            catch (Exception ex) {
                NativeApplicationAPI.WriteLog(2, $"[{tag}] event handler threw\n{ex}");
                throw;
            }
        }
    }

    internal static void Raise<T>(Action<T>? handlers, T arg, string tag) {
        if (handlers == null || NativeApplicationAPI.ReadUpdateInterrupted()) {
            return;
        }
        foreach (Action<T> handler in handlers.GetInvocationList()) {
            try {
                handler(arg);
                if (NativeApplicationAPI.ReadUpdateInterrupted()) { return; }
            }
            catch (Exception ex) {
                NativeApplicationAPI.WriteLog(2, $"[{tag}] event handler threw\n{ex}");
                throw;
            }
        }
    }

    // 次回更新時の購読先へ通知を予約する
    internal static void Enqueue(Action dispatch) {
        deferred.Add(dispatch);
    }

    // 更新開始時に予約済みの通知を実行する
    internal static void FlushDeferred() {

        int count = deferred.Count;
        if (count == 0) {
            return;
        }
        int consumed = 0;
        try {
            while (consumed < count && !NativeApplicationAPI.ReadUpdateInterrupted()) {
                // 失敗したEventを再配信せず、別の未処理Eventは残す
                Action dispatch = deferred[consumed++];
                dispatch();
            }
        }
        finally {
            // callback中に追加されたEventは次回分として保持する
            deferred.RemoveRange(0, consumed);
        }
    }

    // 再読込とStopで旧Assemblyの通知予約を解放する
    internal static void ResetForReload() {
        deferred.Clear();
    }
}
