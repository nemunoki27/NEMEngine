namespace NEMEngine;

// インスタンスイベント。C# の event Action のように += / -= で購読でき、
// 例外隔離・owner 破棄での自動解除・次フレーム遅延発火を備える。
// スクリプトのフィールドに公開して使う（例: public readonly GameEvent OnDied = new();）。
// += / -= は C# の event と同じくこのインスタンスを直接書き換える（戻り値は同一参照）。
// readonly フィールドでは += が使えないため Subscribe / Unsubscribe を使う。main thread 専用。
public sealed class GameEvent {

    private const string Tag = "GameEvent";

    private Action? handlers;

    public int ListenerCount => handlers?.GetInvocationList().Length ?? 0;

    public EventSubscription Subscribe(Action handler) {
        if (handler == null) {
            return EventSubscription.Inactive;
        }
        handlers += handler;
        return new EventSubscription(() => handlers -= handler);
    }

    // owner 破棄時に自動解除される購読。owner の Awake/OnEnable から張る用途
    public EventSubscription Subscribe(MonoBehaviour owner, Action handler) {
        EventSubscription sub = Subscribe(handler);
        EventOwnerTracker.Track(owner, sub);
        return sub;
    }

    public void Unsubscribe(Action handler) {
        handlers -= handler;
    }

    // 即時発火。購読順・例外隔離（1 人が例外を投げても残りは呼ばれる）
    public void Invoke() {
        EventDispatch.Raise(handlers, Tag);
    }

    // 次フレーム頭(Update phase)まで遅延して発火する。配信先は flush 時点の購読者
    public void InvokeDeferred() {
        EventDispatch.Enqueue(Invoke);
    }

    public void Clear() {
        handlers = null;
    }

    public static GameEvent operator +(GameEvent e, Action handler) {
        e.Subscribe(handler);
        return e;
    }

    public static GameEvent operator -(GameEvent e, Action handler) {
        e.Unsubscribe(handler);
        return e;
    }
}

// 引数つきインスタンスイベント。event Action<T> 相当。T は readonly struct 推奨。
public sealed class GameEvent<T> {

    private const string Tag = "GameEvent";

    private Action<T>? handlers;

    public int ListenerCount => handlers?.GetInvocationList().Length ?? 0;

    public EventSubscription Subscribe(Action<T> handler) {
        if (handler == null) {
            return EventSubscription.Inactive;
        }
        handlers += handler;
        return new EventSubscription(() => handlers -= handler);
    }

    public EventSubscription Subscribe(MonoBehaviour owner, Action<T> handler) {
        EventSubscription sub = Subscribe(handler);
        EventOwnerTracker.Track(owner, sub);
        return sub;
    }

    public void Unsubscribe(Action<T> handler) {
        handlers -= handler;
    }

    public void Invoke(T arg) {
        EventDispatch.Raise(handlers, arg, Tag);
    }

    // 遅延発火。arg を捕捉して flush 時点の購読者へ配信する
    public void InvokeDeferred(T arg) {
        T captured = arg;
        EventDispatch.Enqueue(() => Invoke(captured));
    }

    public void Clear() {
        handlers = null;
    }

    public static GameEvent<T> operator +(GameEvent<T> e, Action<T> handler) {
        e.Subscribe(handler);
        return e;
    }

    public static GameEvent<T> operator -(GameEvent<T> e, Action<T> handler) {
        e.Unsubscribe(handler);
        return e;
    }
}
