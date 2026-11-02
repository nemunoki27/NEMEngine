using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using System.Text.Json.Serialization.Metadata;
using System.Reflection;

namespace NEMEngine;

public enum SimpleAnimationMode { Interpolate, Sin, Cos }
public enum SimpleAnimationRepeat { Once, Loop, PingPong }
public enum SimpleAnimationRotationMode { AxisAngle, Euler }

// Inspectorと共通の単一値アニメーション設定
public sealed class SimpleAnimationSettings<T> where T : unmanaged {

    public SimpleAnimationMode Mode { get; set; }
    public SimpleAnimationRepeat Repeat { get; set; }
    public uint CycleCount { get; set; }
    public T Start { get; set; } = SimpleAnimationValues.Default<T>(false);
    public T End { get; set; } = SimpleAnimationValues.Default<T>(true);
    public T Center { get; set; } = SimpleAnimationValues.Default<T>(false);
    public T Amplitude { get; set; } = SimpleAnimationValues.Default<T>(true);
    public Vector3 AngularAmplitude { get; set; }
    public SimpleAnimationRotationMode RotationMode { get; set; }
    public Vector3 StartEuler { get; set; }
    public Vector3 EndEuler { get; set; }
    public Vector3 CenterEuler { get; set; }
    public EasingType Easing { get; set; } = EasingType.Linear;
    public double Duration { get; set; } = 1;
    public double Period { get; set; } = 1;
    public double PhaseDegrees { get; set; }
    public double Delay { get; set; }
    public double Speed { get; set; } = 1;
    public bool UnscaledTime { get; set; }
    public bool StartFromCurrent { get; set; }
    public double BlendDuration { get; set; }
}

internal static class SimpleAnimationSettingsTransfer {

    internal static readonly JsonSerializerOptions Options = new() {
        IncludeFields = true, PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        TypeInfoResolver = new DefaultJsonTypeInfoResolver { Modifiers = { info => {
            if (!info.Type.IsValueType) return;
            for (int i = info.Properties.Count - 1; i >= 0; --i)
                if (info.Properties[i].AttributeProvider is PropertyInfo property && !property.CanWrite) info.Properties.RemoveAt(i);
        } } },
        Converters = { new JsonStringEnumConverter() }
    };

    internal static SimpleAnimationSettings<T> Read<T>(JsonObject value) where T : unmanaged {
        if (value["type"]?.GetValue<string>() != SimpleAnimationValues.Name<T>())
            throw new InvalidOperationException("SimpleAnimationの値の型が一致しません");
        return value.Deserialize<SimpleAnimationSettings<T>>(Options)
            ?? throw new InvalidOperationException("SimpleAnimationの設定を読み込めません");
    }

    internal static JsonObject Write<T>(SimpleAnimationSettings<T> settings) where T : unmanaged {
        ArgumentNullException.ThrowIfNull(settings);
        var result = JsonSerializer.SerializeToNode(settings, Options)!.AsObject();
        result["type"] = SimpleAnimationValues.Name<T>();
        return result;
    }
}
