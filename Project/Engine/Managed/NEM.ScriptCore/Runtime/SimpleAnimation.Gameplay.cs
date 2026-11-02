using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace NEMEngine;

public sealed partial class SimpleAnimation {

    public SimpleAnimationSettings<T> GetSettings<T>() where T : unmanaged =>
        SimpleAnimationSettingsTransfer.Read<T>(ComponentSettingsTransfer.Read(native, 70)["settings"]!.AsObject());

    public void SetSettings<T>(SimpleAnimationSettings<T> settings) where T : unmanaged {
        var values = ComponentSettingsTransfer.Read(native, 70);
        values["settings"] = SimpleAnimationSettingsTransfer.Write(settings);
        ComponentSettingsTransfer.Apply(native, 70, values);
    }

    // nullは自身への適用に戻す
    public void SetTarget(GameObject? target, string componentName, string propertyPath) {
        ArgumentException.ThrowIfNullOrWhiteSpace(componentName);
        ArgumentException.ThrowIfNullOrWhiteSpace(propertyPath);
        var value = GameObject.RawNative(target);
        var values = new JsonObject {
            ["targetNative"] = JsonSerializer.SerializeToNode(value, SimpleAnimationSettingsTransfer.Options),
            ["binding"] = new JsonObject { ["componentName"] = componentName, ["propertyPath"] = propertyPath, ["valueType"] = "Float" }
        };
        ComponentSettingsTransfer.Apply(native, 70, values);
    }

    public void Play() => ControlRequest = 1;
    public void Pause() => ControlRequest = 2;
    public void Resume() => ControlRequest = 3;
    public void Stop() => ControlRequest = 4;
    public void Reset() => ControlRequest = 5;

    public void Seek(float seconds) {
        if (!float.IsFinite(seconds) || seconds < 0) throw new ArgumentOutOfRangeException(nameof(seconds));
        SeekTimeRequest = seconds;
        ControlRequest = 6;
    }

    public T GetValue<T>() where T : unmanaged {
        if (!TryGetValue<T>(out var value)) throw new InvalidOperationException("SimpleAnimationが未評価か、要求した型と一致しません");
        return value;
    }

    public bool TryGetValue<T>(out T value) where T : unmanaged {
        value = default;
        int type = SimpleAnimationValues.Index<T>();
        if (!HasValue || ValueType != type) return false;
        value = type == 7 ? SimpleAnimationValues.FromInt<T>(ValueInteger) : SimpleAnimationValues.FromVector<T>(ValueVector);
        return true;
    }
}

internal static class SimpleAnimationValues {

    internal static int Index<T>() where T : unmanaged {
        if (typeof(T) == typeof(float)) return 0;
        if (typeof(T) == typeof(Vector2)) return 1;
        if (typeof(T) == typeof(Vector3)) return 2;
        if (typeof(T) == typeof(Vector4)) return 3;
        if (typeof(T) == typeof(Color3)) return 4;
        if (typeof(T) == typeof(Color4)) return 5;
        if (typeof(T) == typeof(Quaternion)) return 6;
        if (typeof(T) == typeof(int)) return 7;
        throw new NotSupportedException("SimpleAnimationに対応していない型です");
    }

    internal static string Name<T>() where T : unmanaged => Index<T>() switch {
        0 => "Float", 1 => "Vector2", 2 => "Vector3", 3 => "Vector4", 4 => "Color3", 5 => "Color4", 6 => "Quaternion", _ => "Int"
    };

    internal static T FromInt<T>(int value) where T : unmanaged => Unsafe.As<int, T>(ref value);

    internal static T FromVector<T>(Vector4 value) where T : unmanaged {
        // 対応する型は同じ順序で浮動小数点成分を持つ
        if (Index<T>() == 7) throw new InvalidOperationException("整数は専用の値から取得してください");
        return Unsafe.As<Vector4, T>(ref value);
    }

    internal static T Default<T>(bool one) where T : unmanaged {
        int type = Index<T>();
        if (type == 7) return FromInt<T>(one ? 1 : 0);
        float number = one ? 1 : 0;
        return FromVector<T>(type == 6 ? new Vector4(0, 0, 0, 1) : new Vector4(number, number, number, number));
    }
}
