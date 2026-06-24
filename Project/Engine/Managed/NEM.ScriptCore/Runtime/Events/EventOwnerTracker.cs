namespace NEMEngine;

//============================================================================
//	EventOwnerTracker
//	owner(ScriptBehaviour) ごとに購読を束ね、owner 破棄時に一括解除する。
//	owner 付き Subscribe 経路だけが Track し、HostBridge.ReleaseSlot から CancelOwnedBy される。
//============================================================================
// Timers/Coroutines の owner 紐付けと同じ役割。main thread 専用。
internal static class EventOwnerTracker {

    // owner -> その owner が張った購読群。参照比較で owner を識別する
    private static readonly Dictionary<ScriptBehaviour, List<EventSubscription>> byOwner = new();

    internal static void Track(ScriptBehaviour owner, EventSubscription sub) {
        if (owner == null || sub == null || !sub.IsActive) {
            return;
        }
        if (!byOwner.TryGetValue(owner, out List<EventSubscription>? list)) {
            list = new List<EventSubscription>();
            byOwner[owner] = list;
        }
        // 解除済みハンドルは死蔵するが owner 破棄でまとめて捨てるため実害は無い
        list.Add(sub);
    }

    // owner 破棄時に呼ぶ。束ねた購読を全 Dispose する（Dispose は多重でも安全）
    internal static void CancelOwnedBy(ScriptBehaviour owner) {
        if (owner == null || !byOwner.Remove(owner, out List<EventSubscription>? list)) {
            return;
        }
        foreach (EventSubscription sub in list) {
            sub.Dispose();
        }
    }

    // DLL unload / Play Stop で全 owner の紐付けを捨てる
    internal static void ResetForReload() {
        byOwner.Clear();
    }
}
