using System.Text.Json.Nodes;
using NEMEngine;

namespace NEM.ScriptAnalyzers.Tests;

internal static class SimpleAnimationTests {

    internal static void Run() {
        Check(3.5f);
        Check(int.MaxValue);
        Check(new Vector2(2, 3));
        Check(new Vector3(2, 3, 4));
        Check(new Vector4(2, 3, 4, 5));
        Check(new Color3(0.2f, 0.3f, 0.4f));
        Check(new Color4(0.2f, 0.3f, 0.4f, 0.5f));
        Check(Quaternion.Euler(20, 30, 40));
        var rotation = new SimpleAnimationSettings<Quaternion> { RotationMode = SimpleAnimationRotationMode.Euler, EndEuler = new Vector3(0, 720, 0) };
        var restored = SimpleAnimationSettingsTransfer.Read<Quaternion>(SimpleAnimationSettingsTransfer.Write(rotation));
        if (restored.RotationMode != SimpleAnimationRotationMode.Euler || restored.EndEuler.y != 720)
            throw new InvalidOperationException("Euler rotation winding was lost.");
        var native = JsonNode.Parse("""{"type":"Vector3","mode":"Sin","repeat":"Loop","start":{"x":1,"y":2,"z":3},"end":{"x":4,"y":5,"z":6},"angularAmplitude":{"x":0,"y":90,"z":0},"period":2,"easing":"Linear"}""")!.AsObject();
        var settings = SimpleAnimationSettingsTransfer.Read<Vector3>(native);
        if (settings.Start != new Vector3(1, 2, 3) || settings.Period != 2 || settings.AngularAmplitude.y != 90)
            throw new InvalidOperationException("Native SimpleAnimation settings changed.");
        bool rejected = false;
        try { SimpleAnimationSettingsTransfer.Read<Color4>(native); }
        catch (InvalidOperationException) { rejected = true; }
        if (!rejected) throw new InvalidOperationException("SimpleAnimation accepted an incompatible type.");
    }

    // Nativeと共通の成分名で設定を往復する
    private static void Check<T>(T value) where T : unmanaged {
        var source = new SimpleAnimationSettings<T> { Start = value, End = value, Center = value, Amplitude = value,
            Mode = SimpleAnimationMode.Cos, Repeat = SimpleAnimationRepeat.PingPong, CycleCount = 3, StartFromCurrent = true };
        var json = SimpleAnimationSettingsTransfer.Write(source);
        var result = SimpleAnimationSettingsTransfer.Read<T>(json);
        if (!result.Start.Equals(value) || !result.End.Equals(value) || !result.Center.Equals(value) ||
            !result.Amplitude.Equals(value) || result.Mode != source.Mode || result.Repeat != source.Repeat || result.CycleCount != 3)
            throw new InvalidOperationException("SimpleAnimation roundtrip failed: " + typeof(T).Name);
    }
}
