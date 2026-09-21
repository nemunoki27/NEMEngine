using System.Collections;

namespace NEMEngine;

// Coroutine の opaque handle（index + generation）。
public readonly struct CoroutineHandle : IEquatable<CoroutineHandle> {

    internal readonly int index;
    internal readonly uint generation;

    internal CoroutineHandle(int index, uint generation) {
        this.index = index;
        this.generation = generation;
    }

    public bool IsValid => generation != 0 && Coroutines.IsAlive(this);

    public bool Equals(CoroutineHandle other) => index == other.index && generation == other.generation;
    public override bool Equals(object? obj) => obj is CoroutineHandle other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(index, generation);
}

// yield 待機指示。
public abstract class YieldInstruction {
}

public sealed class WaitForSeconds : YieldInstruction {
    internal readonly float seconds;
    public WaitForSeconds(float seconds) { this.seconds = seconds; }
}

public sealed class WaitForSecondsRealtime : YieldInstruction {
    internal readonly float seconds;
    public WaitForSecondsRealtime(float seconds) { this.seconds = seconds; }
}

public sealed class WaitForFixedUpdate : YieldInstruction {
}

public sealed class WaitForEndOfFrame : YieldInstruction {
}

// どの phase で resume するか。
internal enum CoroutinePhase {
    Update,
    Fixed,
    EndOfFrame
}

// Unity 風 Coroutine service。main thread でのみ resume する。
// Update / FixedUpdate / EndOfFrame の resume queue を phase で分離する。
// owner ScriptBehaviour 破棄 / DLL unload / Play Stop で停止し、IEnumerator 参照を手放す。
internal static class Coroutines {

    private sealed class Routine {
        public uint generation;        // 0 = 空きスロット
        public bool active;
        public ScriptBehaviour? owner;
        public readonly Stack<IEnumerator> stack = new();
        public CoroutinePhase resumePhase;
        public bool waitingTime;       // WaitForSeconds(Realtime) 待機中か
        public float wait;
        public bool waitUnscaled;
    }

    private static readonly List<Routine> routines = new();
    private static readonly Stack<int> freeList = new();
    private static uint nextGeneration = 1;
    // 無限 yield(IEnumerator) ネストの暴走を防ぐ 1 step あたりの上限
    private const int MaxStepsPerResume = 1024;

    internal static bool IsAlive(CoroutineHandle handle) {
        return handle.index >= 0 && handle.index < routines.Count
            && routines[handle.index].active && routines[handle.index].generation == handle.generation;
    }

    internal static CoroutineHandle Start(ScriptBehaviour owner, IEnumerator routine) {
        if (routine == null) {
            return default;
        }
        Routine entry;
        int index;
        if (freeList.Count > 0) {
            index = freeList.Pop();
            entry = routines[index];
            entry.stack.Clear();
        } else {
            index = routines.Count;
            entry = new Routine();
            routines.Add(entry);
        }
        entry.generation = nextGeneration++;
        if (nextGeneration == 0) {
            nextGeneration = 1;
        }
        entry.active = true;
        entry.owner = owner;
        entry.stack.Push(routine);
        entry.resumePhase = CoroutinePhase.Update;
        entry.waitingTime = false;
        entry.wait = 0.0f;
        entry.waitUnscaled = false;

        CoroutineHandle handle = new(index, entry.generation);
        // Unity と同様、最初の yield までは即時に実行する
        if (Step(entry)) {
            FreeAt(index);
        }
        return handle;
    }

    internal static bool Stop(CoroutineHandle handle) {
        if (!IsAlive(handle)) {
            return false;
        }
        FreeAt(handle.index);
        return true;
    }

    internal static void StopAllForOwner(ScriptBehaviour owner) {
        for (int i = 0; i < routines.Count; ++i) {
            if (routines[i].active && ReferenceEquals(routines[i].owner, owner)) {
                FreeAt(i);
            }
        }
    }

    // 指定 phase の resume を実行する（BehaviorSystem から main thread で）。
    internal static void Tick(CoroutinePhase phase) {
        float scaledDelta = Time.DeltaTime;
        float unscaledDelta = Time.UnscaledDeltaTime;
        int count = routines.Count;
        for (int i = 0; i < count && i < routines.Count; ++i) {
            Routine r = routines[i];
            if (!r.active) {
                continue;
            }
            // owner 破棄で停止
            if (r.owner != null && !r.owner.entity.isAlive) {
                FreeAt(i);
                continue;
            }
            if (r.resumePhase != phase) {
                continue;
            }
            if (r.waitingTime) {
                r.wait -= r.waitUnscaled ? unscaledDelta : scaledDelta;
                if (r.wait > 0.0f) {
                    continue;
                }
                r.waitingTime = false;
            }
            if (Step(r)) {
                FreeAt(i);
            }
        }
    }

    // routine を次の待機点まで進める。完了したら true。
    private static bool Step(Routine r) {
        int steps = 0;
        while (r.stack.Count > 0) {
            if (++steps > MaxStepsPerResume) {
                NativeAPI.WriteLog(2, "[Coroutines] step budget exceeded (possible runaway nested yield). stopping routine.");
                return true;
            }
            IEnumerator top = r.stack.Peek();
            bool moved;
            try {
                moved = top.MoveNext();
            }
            catch (Exception ex) {
                NativeAPI.WriteLog(2, $"[Coroutines] routine threw\n{ex}");
                return true; // この routine を停止（他は継続）
            }
            if (!moved) {
                // この階層が終了 → pop して親を継続
                r.stack.Pop();
                continue;
            }
            object? current = top.Current;
            switch (current) {
            case null:
                // yield return null: 次の Update で resume
                r.resumePhase = CoroutinePhase.Update;
                r.waitingTime = false;
                return false;
            case IEnumerator nested:
                // ネスト IEnumerator: stack へ積んで同一 step 内で進める
                r.stack.Push(nested);
                continue;
            case WaitForSeconds wfs:
                r.resumePhase = CoroutinePhase.Update;
                r.waitingTime = true;
                r.waitUnscaled = false;
                r.wait = wfs.seconds;
                return false;
            case WaitForSecondsRealtime wfsr:
                r.resumePhase = CoroutinePhase.Update;
                r.waitingTime = true;
                r.waitUnscaled = true;
                r.wait = wfsr.seconds;
                return false;
            case WaitForFixedUpdate:
                r.resumePhase = CoroutinePhase.Fixed;
                r.waitingTime = false;
                return false;
            case WaitForEndOfFrame:
                r.resumePhase = CoroutinePhase.EndOfFrame;
                r.waitingTime = false;
                return false;
            default:
                // 未知の yield 値は 1 フレーム待機として扱う
                r.resumePhase = CoroutinePhase.Update;
                r.waitingTime = false;
                return false;
            }
        }
        return true; // stack が空 = 完了
    }

    private static void FreeAt(int index) {
        if (index < 0 || index >= routines.Count) {
            return;
        }
        Routine r = routines[index];
        r.active = false;
        r.owner = null;
        // IEnumerator の strong reference を手放す（古い GameScripts assembly を保持しない）
        r.stack.Clear();
        freeList.Push(index);
    }

    // DLL unload / Play Stop で全 coroutine を停止し、参照を手放す。
    internal static void ResetForReload() {
        routines.Clear();
        freeList.Clear();
        nextGeneration = 1;
    }
}
