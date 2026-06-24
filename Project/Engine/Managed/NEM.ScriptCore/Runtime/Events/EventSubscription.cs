using System.Threading;

namespace NEMEngine;

// 購読ハンドル。Dispose で解除する。多重 Dispose は安全（2 回目以降はノーオペ）。
// owner 付き Subscribe では owner 破棄時にこのハンドルが Dispose される。
public sealed class EventSubscription : IDisposable {

    // 何もしない購読。null handler など無効な Subscribe の戻り値に使う
    public static readonly EventSubscription Inactive = new();

    private Action? unsubscribe;

    internal EventSubscription(Action unsubscribe) {
        this.unsubscribe = unsubscribe;
    }

    // Inactive 用。解除アクションを持たない
    private EventSubscription() {
        unsubscribe = null;
    }

    public bool IsActive => unsubscribe != null;

    public void Dispose() {

        // 解除アクションを 1 度だけ取り出して実行する。再入や多重 Dispose でも二重解除しない
        Action? action = Interlocked.Exchange(ref unsubscribe, null);
        action?.Invoke();
    }
}
