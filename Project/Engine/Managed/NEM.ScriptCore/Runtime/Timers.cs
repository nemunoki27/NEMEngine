namespace NEMEngine;

// 低コスト Timer service。main thread でのみ tick / callback する（BehaviorSystem の Update phase）。
// scaled timer は TimeScale に追従し、unscaled timer は pause 中も進む。
// DLL unload / Play Stop で全 cancel。owner script 紐付け timer は owner 破棄で cancel。
public readonly struct TimerHandle : IEquatable<TimerHandle> {

    internal readonly int index;
    internal readonly uint generation;

    internal TimerHandle(int index, uint generation) {
        this.index = index;
        this.generation = generation;
    }

    public bool IsValid => generation != 0 && Timers.IsAlive(this);

    public bool Equals(TimerHandle other) => index == other.index && generation == other.generation;
    public override bool Equals(object? obj) => obj is TimerHandle other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(index, generation);
}

public static class Timers {

    private struct Entry {
        public uint generation;   // 0 = 空きスロット
        public bool active;
        public float remaining;
        public float interval;
        public bool repeating;
        public bool unscaled;
        public Action? callback;
        public ScriptBehaviour? owner; // null = global（owner 破棄では cancel されない）
    }

    private static readonly List<Entry> entries = new();
    private static readonly Stack<int> freeList = new();
    private static uint nextGeneration = 1;

    //========================================================================
    //	public API
    //========================================================================
    public static TimerHandle Schedule(float delaySeconds, Action callback) => Add(delaySeconds, false, false, callback, null);
    public static TimerHandle ScheduleRepeating(float intervalSeconds, Action callback) => Add(intervalSeconds, true, false, callback, null);
    public static TimerHandle ScheduleUnscaled(float delaySeconds, Action callback) => Add(delaySeconds, false, true, callback, null);

    public static bool Cancel(TimerHandle handle) {
        if (!IsAlive(handle)) {
            return false;
        }
        Free(handle.index);
        return true;
    }

    //========================================================================
    //	internal（owner 紐付け / tick / lifecycle）
    //========================================================================
    internal static TimerHandle Schedule(float delaySeconds, Action callback, ScriptBehaviour owner) => Add(delaySeconds, false, false, callback, owner);
    internal static TimerHandle ScheduleRepeating(float intervalSeconds, Action callback, ScriptBehaviour owner) => Add(intervalSeconds, true, false, callback, owner);
    internal static TimerHandle ScheduleUnscaled(float delaySeconds, Action callback, ScriptBehaviour owner) => Add(delaySeconds, false, true, callback, owner);

    internal static bool IsAlive(TimerHandle handle) {
        return handle.index >= 0 && handle.index < entries.Count
            && entries[handle.index].active && entries[handle.index].generation == handle.generation;
    }

    private static TimerHandle Add(float seconds, bool repeating, bool unscaled, Action callback, ScriptBehaviour? owner) {
        if (callback == null || seconds < 0.0f || float.IsNaN(seconds) || float.IsInfinity(seconds)) {
            return default;
        }
        Entry entry = new() {
            generation = nextGeneration++,
            active = true,
            remaining = seconds,
            interval = seconds,
            repeating = repeating,
            unscaled = unscaled,
            callback = callback,
            owner = owner,
        };
        if (nextGeneration == 0) {
            nextGeneration = 1; // 0 は無効値なので飛ばす
        }
        int index;
        if (freeList.Count > 0) {
            index = freeList.Pop();
            entries[index] = entry;
        } else {
            index = entries.Count;
            entries.Add(entry);
        }
        return new TimerHandle(index, entry.generation);
    }

    private static void Free(int index) {
        if (index < 0 || index >= entries.Count) {
            return;
        }
        Entry e = entries[index];
        e.active = false;
        e.callback = null;
        e.owner = null;
        entries[index] = e;
        freeList.Push(index);
    }

    // BehaviorSystem の Update phase から main thread で呼ばれる。
    internal static void Tick() {
        float scaledDelta = Time.DeltaTime;
        float unscaledDelta = Time.UnscaledDeltaTime;
        // 走査中に追加された timer（index >= count）は次フレームから。cancel は active=false で安全。
        int count = entries.Count;
        for (int i = 0; i < count && i < entries.Count; ++i) {
            Entry e = entries[i];
            if (!e.active || e.callback == null) {
                continue;
            }
            // owner 紐付け timer は owner が生存していなければ cancel
            if (e.owner != null && !e.owner.entity.isAlive) {
                Free(i);
                continue;
            }
            e.remaining -= e.unscaled ? unscaledDelta : scaledDelta;
            if (e.remaining > 0.0f) {
                entries[i] = e;
                continue;
            }

            Action callback = e.callback;
            if (e.repeating) {
                // 次周期へ。interval 0 の暴走を防ぐため最低 1 フレーム分は空ける
                e.remaining += e.interval > 0.0f ? e.interval : float.Epsilon;
                entries[i] = e;
            } else {
                // 単発は callback 実行前に解放しておく（再入 cancel/再 schedule を安全にする）
                Free(i);
            }

            try {
                callback();
            }
            catch (Exception ex) {
                NativeAPI.WriteLog(2, $"[Timers] callback threw\n{ex}");
            }
        }
    }

    // owner script 破棄時に、その owner に紐づく timer を cancel する（ReleaseSlot から呼ぶ）。
    internal static void CancelOwnedBy(ScriptBehaviour owner) {
        for (int i = 0; i < entries.Count; ++i) {
            if (entries[i].active && ReferenceEquals(entries[i].owner, owner)) {
                Free(i);
            }
        }
    }

    // DLL unload / Play Stop で全 timer を破棄し、delegate 参照を手放す。
    internal static void ResetForReload() {
        entries.Clear();
        freeList.Clear();
        nextGeneration = 1;
    }
}
