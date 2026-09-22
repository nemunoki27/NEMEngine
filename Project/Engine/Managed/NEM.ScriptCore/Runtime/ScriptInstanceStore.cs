namespace NEMEngine;

// ホスト寿命で世代履歴を保持する格納庫
internal sealed unsafe class ScriptInstanceStore {

    internal readonly List<ScriptInstanceSlot> slots = new();
    internal readonly Stack<uint> freeSlots = new();

    internal NativeScriptInstanceHandle AllocateSlot(ScriptBehaviour script) {

        if (freeSlots.Count > 0) {

            uint index = freeSlots.Pop();
            ScriptInstanceSlot reused = slots[(int)index];
            // generationはrelease時に進めた値（必ず1以上、retiredは積まれない）
            reused.instance = script;
            reused.inUse = true;
            return new NativeScriptInstanceHandle(index, reused.generation);
        }

        // 空きが無ければ新しい枠を追加する。generationは1始まり
        var slot = new ScriptInstanceSlot { generation = 1, instance = script, inUse = true, retired = false };
        slots.Add(slot);
        return new NativeScriptInstanceHandle((uint)(slots.Count - 1), slot.generation);
    }

    internal bool TryResolveSlot(NativeScriptInstanceHandle handle, out ScriptBehaviour script) {

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

        // owner script 破棄時に、その owner に紐づく coroutine / timer / event 購読を停止・解除する
        if (slot.instance != null) {
            ScriptServiceLifetime.EndOwner(slot.instance);
        }
        slot.instance = null;
        slot.runtimeStateSnapshot = null;
        slot.inUse = false;
        RetireOrRecycle(slot, handle.index);
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
            slot.instance = null;
            slot.runtimeStateSnapshot = null;
            slot.inUse = false;
            RetireOrRecycle(slot, (uint)i);
        }
    }

    internal ScriptBehaviour? FindScriptOfTypeByType(Type type) {

        foreach (ScriptInstanceSlot slot in slots) {
            if (slot.inUse && !slot.retired && slot.instance != null && type.IsInstanceOfType(slot.instance)) {
                return slot.instance;
            }
        }
        return null;
    }

    internal List<ScriptBehaviour> FindScriptsOfTypeByType(Type type) {

        var result = new List<ScriptBehaviour>();
        foreach (ScriptInstanceSlot slot in slots) {
            if (slot.inUse && !slot.retired && slot.instance != null && type.IsInstanceOfType(slot.instance)) {
                result.Add(slot.instance);
            }
        }
        return result;
    }

    internal T? FindScriptOfType<T>() where T : ScriptBehaviour {

        foreach (ScriptInstanceSlot slot in slots) {
            if (slot.inUse && !slot.retired && slot.instance is T match) {
                return match;
            }
        }
        return null;
    }

    internal T[] FindScriptsOfType<T>() where T : ScriptBehaviour {

        var result = new List<T>();
        foreach (ScriptInstanceSlot slot in slots) {
            if (slot.inUse && !slot.retired && slot.instance is T match) {
                result.Add(match);
            }
        }
        return result.ToArray();
    }
}
