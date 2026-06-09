using System.Runtime.InteropServices;

namespace NEMEngine;

// C++側 ManagedWorldHandle と同一レイアウト。生ポインタの代わりに世代付きハンドルを持つ
[StructLayout(LayoutKind.Sequential)]
public struct ManagedWorldHandle {

    public uint index;
    public uint generation;

    public static ManagedWorldHandle Null => new() {
        index = 0xffffffffu,
        generation = 0
    };

    public bool isValid => index != 0xffffffffu;
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeEntity {

    public ManagedWorldHandle world;
    public uint index;
    public uint generation;

    public static NativeEntity Null => new() {
        world = ManagedWorldHandle.Null,
        index = 0xffffffffu,
        generation = 0
    };
}

public readonly struct Entity {

    internal readonly NativeEntity native;

    public static Entity nullEntity => new(NativeEntity.Null);

    internal Entity(NativeEntity native) {
        this.native = native;
    }

    // 生ポインタ判定ではなく、world handleとentity indexの有効値で判定する
    public bool isValid => native.world.isValid && native.index != 0xffffffffu;
    public bool isAlive => isValid && NativeApi.ReadIsAlive(native);

    public string name {
        get => NativeApi.ReadName(native);
        set => NativeApi.WriteName(native, value);
    }

    public bool activeSelf {
        get => NativeApi.ReadActiveSelf(native);
        set => NativeApi.WriteActiveSelf(native, value);
    }

    public bool activeInHierarchy => NativeApi.ReadActiveInHierarchy(native);
    public Entity parent {
        get => NativeApi.ReadParent(native);
        set => NativeApi.WriteParent(native, value.native);
    }
    public Entity firstChild => NativeApi.ReadFirstChild(native);
    public Entity nextSibling => NativeApi.ReadNextSibling(native);
    public Transform transform => new(this);

    public void SetActive(bool active) {
        activeSelf = active;
    }

    public void SetParent(Entity parent) {
        this.parent = parent;
    }
}
