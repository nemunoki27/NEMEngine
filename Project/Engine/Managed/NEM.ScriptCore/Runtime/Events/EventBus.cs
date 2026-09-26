namespace NEMEngine;

//============================================================================
//	EventBus
//	型をキーにしたグローバル pub/sub。GameObject に依存しないゲーム全体イベント向け。
//	イベント型は readonly struct 推奨。typeof(T) 完全一致で配信し、基底/派生への
//	fan-out は行わない。例外隔離・owner 破棄での自動解除・次フレーム遅延発火を備える。
//============================================================================
// main thread 専用。delegate スナップショットで Publish 中の購読変更・再入も安全。
public static class EventBus {

    // 1 イベント型ぶんの購読集合。Action<T> のマルチキャストで購読順とスナップショットを得る
    private abstract class Channel {
        internal abstract void Clear();
    }

    private sealed class Channel<T> : Channel {
        internal Action<T>? handlers;
        internal override void Clear() => handlers = null;
    }

    private const string Tag = "EventBus";

    // typeof(T) -> Channel<T>
    private static readonly Dictionary<Type, Channel> channels = new();

    public static EventSubscription Subscribe<T>(Action<T> handler) {
        if (handler == null) {
            return EventSubscription.Inactive;
        }
        Channel<T> channel = GetOrCreate<T>();
        channel.handlers += handler;
        return new EventSubscription(() => channel.handlers -= handler);
    }

    // owner 破棄時に自動解除される購読
    public static EventSubscription Subscribe<T>(MonoBehaviour owner, Action<T> handler) {
        EventSubscription sub = Subscribe(handler);
        EventOwnerTracker.Track(owner, sub);
        return sub;
    }

    public static void Unsubscribe<T>(Action<T> handler) {
        if (channels.TryGetValue(typeof(T), out Channel? c) && c is Channel<T> channel) {
            channel.handlers -= handler;
        }
    }

    // 即時配信。購読者ゼロなら何もしない
    public static void Publish<T>(in T evt) {
        if (channels.TryGetValue(typeof(T), out Channel? c) && c is Channel<T> channel) {
            EventDispatch.Raise(channel.handlers, evt, Tag);
        }
    }

    // 次フレーム頭(Update phase)まで遅延して配信する。配信先は flush 時点の購読者
    public static void PublishDeferred<T>(in T evt) {
        T captured = evt;
        EventDispatch.Enqueue(() => Publish(captured));
    }

    public static void Clear<T>() {
        if (channels.TryGetValue(typeof(T), out Channel? c)) {
            c.Clear();
        }
    }

    public static int ListenerCount<T>() {
        return channels.TryGetValue(typeof(T), out Channel? c) && c is Channel<T> channel
            ? channel.handlers?.GetInvocationList().Length ?? 0
            : 0;
    }

    // DLL unload / Play Stop で全チャンネルを捨て、古い GameScripts assembly の delegate を手放す
    internal static void ResetForReload() {
        channels.Clear();
    }

    private static Channel<T> GetOrCreate<T>() {
        if (!channels.TryGetValue(typeof(T), out Channel? c) || c is not Channel<T> channel) {
            channel = new Channel<T>();
            channels[typeof(T)] = channel;
        }
        return channel;
    }
}
