using System.Runtime.InteropServices;

namespace NEMEngine;

[StructLayout(LayoutKind.Sequential)]
public struct NativeEntity {

    public nuint world;
    public uint index;
    public uint generation;
}

public readonly struct Entity {

    internal readonly NativeEntity native;

    internal Entity(NativeEntity native) {
        this.native = native;
    }

    public bool isValid => native.index != 0xffffffffu;
}
