using System.Text.Json;
using NEMEngine;

namespace NEM.ScriptAnalyzers.Tests;

internal static class MotionAnimationTests {
    internal static void Run() {
        // Inspectorと同じ数値EnumとVector成分で往復する。
        var source = new MotionAnimationSettings {
            Mode = MotionAnimationMode.Segments, Plane = MotionAnimationPlane.GroundXZ,
            Repeat = SimpleAnimationRepeat.PingPong, CycleCount = 2,
            Segments = [new() { Offset = new(1, 0, 2), Duration = 0.25f }, new() { Offset = new(-1, 0, 4), Duration = 0.5f }]
        };
        var json = JsonSerializer.SerializeToNode(source, MotionAnimationTransfer.Options)!;
        var restored = json.Deserialize<MotionAnimationSettings>(MotionAnimationTransfer.Options)!;
        if (json["mode"]!.GetValue<int>() != 2 || restored.Segments.Length != 2 ||
            restored.Segments[1].Offset.z != 4 || restored.CycleCount != 2)
            throw new InvalidOperationException("MotionAnimation設定の往復に失敗しました");
        // 通知の回数を32bitへ切り詰めずに読み取る。
        var events = JsonSerializer.Deserialize<MotionAnimationEvents>("""{"segments":[4294967296,2],"cycles":3,"completions":1}""", MotionAnimationTransfer.Options)!;
        if (events.Segments[0] != 4294967296UL || events.Completions != 1)
            throw new InvalidOperationException("MotionAnimationの通知を復元できません");
    }
}
