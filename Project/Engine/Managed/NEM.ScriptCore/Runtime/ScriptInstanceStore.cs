namespace NEMEngine;

// ホスト寿命で世代履歴を保持する格納庫
internal sealed unsafe class ScriptInstanceStore {

    internal readonly List<ScriptInstanceSlot> slots = new();
    internal readonly Stack<uint> freeSlots = new();

    internal NativeScriptInstanceHandle AllocateSlot(MonoBehaviour script) {

        if (freeSlots.Count > 0) {

            uint index = freeSlots.Pop();
            ScriptInstanceSlot reused = slots[(int)index];
            // generationはrelease時に進めた値（必ず1以上、retiredは積まれない）
            reused.instance = script;
            reused.inUse = true;
            script.instanceAttached = true;
            return new NativeScriptInstanceHandle(index, reused.generation);
        }

        // 空きが無ければ新しい枠を追加する。generationは1始まり
        var slot = new ScriptInstanceSlot { generation = 1, instance = script, inUse = true, retired = false };
        slots.Add(slot);
        script.instanceAttached = true;
        return new NativeScriptInstanceHandle((uint)(slots.Count - 1), slot.generation);
    }

    internal bool TryResolveSlot(NativeScriptInstanceHandle handle, out MonoBehaviour script) {

        script = null!;
        if (!handle.IsValid || handle.index >= (uint)slots.Count) {
            return false;
        }
        ScriptInstanceSlot slot = slots[(int)handle.index];
        if (slot.retired || !slot.inUse || slot.instance is null || slot.generation != handle.generation) {
            return false;
        }
        script = slot.instance;
        return true;
    }

    internal void ReleaseSlot(NativeScriptInstanceHandle handle) {

        if (!handle.IsValid || handle.index >= (uint)slots.Count) {
            return;
        }
        ScriptInstanceSlot slot = slots[(int)handle.index];
        if (slot.retired || !slot.inUse || slot.generation != handle.generation) {
            return;
        }

        // 枠を無効化してから所有サービスを終了する
        MonoBehaviour? released = slot.instance;
        if (released is not null) {
            released.instanceAttached = false;
        }
        slot.instance = null;
        slot.runtimeStateSnapshot = null;
        slot.inUse = false;
        RetireOrRecycle(slot, handle.index);
        if (released is not null) {
            ScriptServiceLifetime.EndOwner(released);
        }
    }

    internal void RetireOrRecycle(ScriptInstanceSlot slot, uint index) {

        if (slot.generation == uint.MaxValue) {

            // wraparoundすると過去handleとgenerationが再一致し得るため、この枠は永久欠番にする
            slot.retired = true;
            return;
        }
        ++slot.generation;
        freeSlots.Push(index);
    }

    internal void ReleaseAllSlots() {

        freeSlots.Clear();
        for (int i = 0; i < slots.Count; ++i) {

            ScriptInstanceSlot slot = slots[i];
            if (slot.retired) {
                // 永久欠番はfree listへ戻さない
                continue;
            }
            if (slot.instance is not null) {
                slot.instance.instanceAttached = false;
            }
            slot.instance = null;
            slot.runtimeStateSnapshot = null;
            slot.inUse = false;
            RetireOrRecycle(slot, (uint)i);
        }
    }

    // 同じ所有Entityの基底型・interfaceを含むScriptを集める
    internal void AppendScriptsAs<T>(NativeEntity owner, List<T> result) where T : class {
        foreach (ScriptInstanceSlot slot in slots) {
            if (slot.inUse && !slot.retired && slot.instance != null && slot.instance is T match && SameOwner(slot.instance, owner)) {
                result.Add(match);
            }
        }
    }

    internal T? FindScriptAs<T>(NativeEntity owner) where T : class {
        foreach (ScriptInstanceSlot slot in slots) {
            if (slot.inUse && !slot.retired && slot.instance != null && slot.instance is T match && SameOwner(slot.instance, owner)) {
                return match;
            }
        }
        return null;
    }

    private static bool SameOwner(MonoBehaviour script, NativeEntity owner) {
        NativeEntity candidate = GameObject.RawNative(script.ownerReference);
        return candidate.world.index == owner.world.index && candidate.world.generation == owner.world.generation &&
            candidate.index == owner.index && candidate.generation == owner.generation;
    }

    internal MonoBehaviour? FindScriptOfTypeByType(Type type) {

        foreach (ScriptInstanceSlot slot in slots) {
            if (slot.inUse && !slot.retired && slot.instance != null && type.IsInstanceOfType(slot.instance)) {
                return slot.instance;
            }
        }
        return null;
    }

    internal List<MonoBehaviour> FindScriptsOfTypeByType(Type type) {

        var result = new List<MonoBehaviour>();
        foreach (ScriptInstanceSlot slot in slots) {
            if (slot.inUse && !slot.retired && slot.instance != null && type.IsInstanceOfType(slot.instance)) {
                result.Add(slot.instance);
            }
        }
        return result;
    }

    internal T? FindScriptOfType<T>() where T : MonoBehaviour {

        foreach (ScriptInstanceSlot slot in slots) {
            if (slot.inUse && !slot.retired && slot.instance is T match) {
                return match;
            }
        }
        return null;
    }

    internal T[] FindScriptsOfType<T>() where T : MonoBehaviour {

        var result = new List<T>();
        foreach (ScriptInstanceSlot slot in slots) {
            if (slot.inUse && !slot.retired && slot.instance is T match) {
                result.Add(match);
            }
        }
        return result.ToArray();
    }
}
