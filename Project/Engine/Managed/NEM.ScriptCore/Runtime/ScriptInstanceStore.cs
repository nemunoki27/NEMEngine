namespace NEMEngine;

// ホスト寿命で世代履歴を保持する格納庫
internal sealed unsafe class ScriptInstanceStore {

    internal readonly List<ScriptInstanceSlot> slots = new();
    internal readonly Stack<uint> freeSlots = new();

    // 空きslotを再利用して現在の世代を返す
    internal NativeScriptInstanceHandle AllocateSlot(MonoBehaviour script) {

        if (freeSlots.Count > 0) {

            uint index = freeSlots.Pop();
            ScriptInstanceSlot reused = slots[(int)index];
            // 解放時に進めた世代で個体を接続する
            reused.instance = script;
            reused.inUse = true;
            script.instanceAttached = true;
            return new NativeScriptInstanceHandle(index, reused.generation);
        }

        // 空きがなければ世代1の枠を追加する
        var slot = new ScriptInstanceSlot { generation = 1, instance = script, inUse = true, retired = false };
        slots.Add(slot);
        script.instanceAttached = true;
        return new NativeScriptInstanceHandle((uint)(slots.Count - 1), slot.generation);
    }

    // 生存状態と世代が一致する個体を返す
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

    // 個体参照を解除して所有サービスを終了する
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
        slot.savedStateSnapshot = null;
        slot.reloadStateSnapshot = null;
        slot.inUse = false;
        RetireOrRecycle(slot, handle.index);
        if (released is not null) {
            ScriptServiceLifetime.EndOwner(released);
        }
    }

    // 世代が枯渇した枠を除いて再利用する
    internal void RetireOrRecycle(ScriptInstanceSlot slot, uint index) {

        if (slot.generation == uint.MaxValue) {

            // 世代の循環で過去の参照と一致させない
            slot.retired = true;
            return;
        }
        ++slot.generation;
        freeSlots.Push(index);
    }

    // 世代履歴を残して全個体の参照を解除する
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
            slot.savedStateSnapshot = null;
            slot.reloadStateSnapshot = null;
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

    // 所有Entityから代入可能なScriptを探す
    internal T? FindScriptAs<T>(NativeEntity owner) where T : class {

        foreach (ScriptInstanceSlot slot in slots) {
            if (slot.inUse && !slot.retired && slot.instance != null && slot.instance is T match && SameOwner(slot.instance, owner)) {
                return match;
            }
        }
        return null;
    }

    // 同型Scriptが複数ある場合も保存slotを優先して解決する
    internal MonoBehaviour? FindScriptByIdentity(NativeEntity owner, Type type, ulong scriptSlotID) {

        MonoBehaviour? result = null;
        foreach (ScriptInstanceSlot slot in slots) {
            if (!slot.inUse || slot.retired || slot.instance is null || slot.instance.GetType() != type ||
                !SameOwner(slot.instance, owner) || (scriptSlotID != 0 && slot.instance.scriptSlotID != scriptSlotID)) {
                continue;
            }
            if (result is not null) {
                return null;
            }
            result = slot.instance;
        }
        return result;
    }

    // 代入可能な型のScriptを1件返す
    internal MonoBehaviour? FindScriptOfTypeByType(Type type) {

        foreach (ScriptInstanceSlot slot in slots) {
            if (slot.inUse && !slot.retired && slot.instance != null && type.IsInstanceOfType(slot.instance)) {
                return slot.instance;
            }
        }
        return null;
    }

    // 代入可能な型のScriptを集める
    internal List<MonoBehaviour> FindScriptsOfTypeByType(Type type) {

        var result = new List<MonoBehaviour>();
        foreach (ScriptInstanceSlot slot in slots) {
            if (slot.inUse && !slot.retired && slot.instance != null && type.IsInstanceOfType(slot.instance)) {
                result.Add(slot.instance);
            }
        }
        return result;
    }

    // 指定した型のScriptを1件返す
    internal T? FindScriptOfType<T>() where T : MonoBehaviour {

        foreach (ScriptInstanceSlot slot in slots) {
            if (slot.inUse && !slot.retired && slot.instance is T match) {
                return match;
            }
        }
        return null;
    }

    // 指定した型のScriptを配列で返す
    internal T[] FindScriptsOfType<T>() where T : MonoBehaviour {

        var result = new List<T>();
        foreach (ScriptInstanceSlot slot in slots) {
            if (slot.inUse && !slot.retired && slot.instance is T match) {
                result.Add(match);
            }
        }
        return result.ToArray();
    }

    // WorldとEntityの世代を含めて所有元を照合する
    private static bool SameOwner(MonoBehaviour script, NativeEntity owner) {

        NativeEntity candidate = GameObject.RawNative(script.ownerReference);
        return candidate.world.index == owner.world.index && candidate.world.generation == owner.world.generation &&
            candidate.index == owner.index && candidate.generation == owner.generation;
    }
}
