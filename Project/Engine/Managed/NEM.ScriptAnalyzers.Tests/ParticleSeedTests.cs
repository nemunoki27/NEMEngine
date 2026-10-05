using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using NEMEngine;

namespace NEM.ScriptAnalyzers.Tests;

// Seedの明示設定と自動生成値の取得を接続境界で確認する
internal static unsafe class ParticleSeedTests {

    private static uint configuredSeed;
    private static bool automatic;

    internal static void Run() {

        var previousAlive = NativeAPI.IsAlive;
        var previousInstance = NativeAPI.GetComponentInstanceID;
        var previousGet = NativeAPI.GetComponentProperty;
        var previousSet = NativeAPI.SetComponentProperty;
        var previousState = NativeAPI.ParticleSystemState;
        NativeAPI.IsAlive = &Alive;
        NativeAPI.GetComponentInstanceID = &Instance;
        NativeAPI.GetComponentProperty = &Get;
        NativeAPI.SetComponentProperty = &Set;
        NativeAPI.ParticleSystemState = &State;
        try {
            automatic = true;
            configuredSeed = 0;
            NativeEntity entity = new() { world = new() { index = 79, generation = 4 }, index = 1, generation = 2 };
            var particles = ParticleSystem.FromEntity(GameObject.FromNative(entity)!);
            Check(particles.randomSeed == 0xF1234567u);
            particles.randomSeed = 0xFEDCBA98u;
            Check(!particles.useAutoRandomSeed && !automatic && configuredSeed == 0xFEDCBA98u && particles.randomSeed == configuredSeed);
            particles.useAutoRandomSeed = true;
            Check(particles.randomSeed == 0xF1234567u && configuredSeed == 0xFEDCBA98u);
        } finally {
            NativeAPI.IsAlive = previousAlive;
            NativeAPI.GetComponentInstanceID = previousInstance;
            NativeAPI.GetComponentProperty = previousGet;
            NativeAPI.SetComponentProperty = previousSet;
            NativeAPI.ParticleSystemState = previousState;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int Alive(NativeEntity entity) => entity.world.index == 79 ? 1 : 0;
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static ulong Instance(NativeEntity entity, int typeID) => typeID == ParticleSystem.componentTypeID ? 42ul : 0;
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int Get(NativeEntity entity, int typeID, int propertyID, void* value, int size) {
        if (propertyID == 11) { *(int*)value = automatic ? 1 : 0; }
        else if (propertyID == 6) { *(uint*)value = configuredSeed; }
        else { return (int)ManagedStatus.InvalidArgument; }
        return (int)ManagedStatus.Ok;
    }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int Set(NativeEntity entity, int typeID, int propertyID, void* value, int size) {
        if (propertyID == 11) { automatic = *(int*)value != 0; }
        else if (propertyID == 6) { configuredSeed = *(uint*)value; }
        else { return (int)ManagedStatus.InvalidArgument; }
        return (int)ManagedStatus.Ok;
    }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int State(NativeEntity entity, int state, int withChildren) => state == 6 ? unchecked((int)0xF1234567u) : 0;
    private static void Check(bool value) {
        if (!value) { throw new InvalidOperationException("Particle seed contract failed."); }
    }
}
