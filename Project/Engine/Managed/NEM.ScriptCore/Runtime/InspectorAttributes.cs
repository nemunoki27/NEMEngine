namespace NEMEngine;

// Inspector 表示・編集の挙動を制御する属性群。
// すべて source generator が schema metadata へ反映し、C++ Inspector は metadata を参照して描画する。
// gameplay frame では属性 reflection を行わない（schema は build/reload 時にだけ構築する）。

// serializer 対象だが Inspector には表示しない。値は保存される。
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false, Inherited = true)]
public sealed class HideInInspectorAttribute : Attribute {
}

// 数値型の表示範囲。slider / clamp 付き drag に使い、保存時の validation でも参照する。
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false, Inherited = true)]
public sealed class RangeAttribute : Attribute {

    public RangeAttribute(float min, float max) {
        Min = min;
        Max = max;
    }

    public float Min { get; }
    public float Max { get; }
}

// 数値型の下限。下限 validation に使う。
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false, Inherited = true)]
public sealed class MinAttribute : Attribute {

    public MinAttribute(float min) {
        Min = min;
    }

    public float Min { get; }
}

// 数値 drag widget の速度。未指定は型ごとの default。
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false, Inherited = true)]
public sealed class DragSpeedAttribute : Attribute {

    public DragSpeedAttribute(float speed) {
        Speed = speed;
    }

    public float Speed { get; }
}

// 値を表示するが Inspector からは編集不可。serialization 自体は維持する。
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false, Inherited = true)]
public sealed class ReadOnlyAttribute : Attribute {
}

// string の複数行編集。
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false, Inherited = true)]
public sealed class MultilineAttribute : Attribute {
}

// field 上に表示する見出し。Editor 表示専用。
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false, Inherited = true)]
public sealed class HeaderAttribute : Attribute {

    public HeaderAttribute(string header) {
        Header = header;
    }

    public string Header { get; }
}

// field のホバー時に表示する説明。Editor 表示専用。
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false, Inherited = true)]
public sealed class TooltipAttribute : Attribute {

    public TooltipAttribute(string tooltip) {
        Tooltip = tooltip;
    }

    public string Tooltip { get; }
}
