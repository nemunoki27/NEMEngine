using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using NEMEngine;

namespace NEM.ScriptAnalyzers.Tests;

// UTF8名と型付き値が接続境界で変わらないことを確認する
internal static unsafe class AnimationParameterTests {

    private static readonly Dictionary<string, (int Type, float Number, int Integer)> values = new();
    private static NativeEntity receivedEntity;

    internal static void Run() {

        var previousSet = NativeAPI.SetAnimatorParameter;
        var previousGet = NativeAPI.GetAnimatorParameter;
        NativeAPI.SetAnimatorParameter = &Set;
        NativeAPI.GetAnimatorParameter = &Get;
        NativeEntity entity = new() { world = new() { index = 71, generation = 3 }, index = 14, generation = 5 };
        try {

            NativeAnimatorAPI.SetParameter(entity, "移動速度", 0, 2.75f, 0);
            NativeAnimatorAPI.SetParameter(entity, "状態番号", 1, 0.0f, -7);
            NativeAnimatorAPI.SetParameter(entity, "接地", 2, 0.0f, 1);
            NativeAnimatorAPI.SetParameter(entity, "跳躍", 3, 0.0f, 1);
            NativeAnimatorAPI.GetParameter(entity, "移動速度", 0, out float speed, out _);
            NativeAnimatorAPI.GetParameter(entity, "状態番号", 1, out _, out int state);
            NativeAnimatorAPI.GetParameter(entity, "接地", 2, out _, out int grounded);
            NativeAnimatorAPI.SetParameter(entity, "跳躍", 3, 0.0f, 0);
            Check(speed == 2.75f && state == -7 && grounded == 1 && values["跳躍"].Integer == 0);
            Check(receivedEntity.world.index == 71 && receivedEntity.world.generation == 3 &&
                receivedEntity.index == 14 && receivedEntity.generation == 5);

            // 型が違う取得は古い値を返さない
            NativeAnimatorAPI.GetParameter(entity, "状態番号", 0, out float missing, out int missingInteger);
            Check(missing == 0.0f && missingInteger == 0);
            bool rejected = false;
            try { NativeAnimatorAPI.SetParameter(entity, string.Empty, 0, 0.0f, 0); }
            catch (ArgumentException) { rejected = true; }
            Check(rejected && values.Count == 4);
        } finally {

            NativeAPI.SetAnimatorParameter = previousSet;
            NativeAPI.GetAnimatorParameter = previousGet;
            values.Clear();
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int Set(NativeEntity entity, byte* name, int type, float number, int integer) {

        receivedEntity = entity;
        string key = Marshal.PtrToStringUTF8((IntPtr)name) ?? string.Empty;
        values[key] = (type, number, integer);
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int Get(NativeEntity entity, byte* name, int type, float* number, int* integer) {

        receivedEntity = entity;
        string key = Marshal.PtrToStringUTF8((IntPtr)name) ?? string.Empty;
        if (!values.TryGetValue(key, out var value) || value.Type != type) return 0;
        *number = value.Number;
        *integer = value.Integer;
        return 1;
    }

    private static void Check(bool condition) {

        if (!condition) throw new InvalidOperationException("Animation parameter ABI contract failed.");
    }
}
