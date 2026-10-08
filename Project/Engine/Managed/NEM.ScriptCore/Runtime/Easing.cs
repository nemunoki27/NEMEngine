namespace NEMEngine;

// イージングの種類。C++側 EasingType（Easing.h）と定義順を一致させる。
public enum EasingType {

    // そのまま
    Linear,

    // EaseIn
    EaseInSine,
    EaseInQuad,
    EaseInCubic,
    EaseInQuart,
    EaseInQuint,
    EaseInExpo,
    EaseInCirc,
    EaseInBack,
    EaseInBounce,

    // EaseOut
    EaseOutSine,
    EaseOutQuad,
    EaseOutCubic,
    EaseOutQuart,
    EaseOutQuint,
    EaseOutExpo,
    EaseOutCirc,
    EaseOutBack,
    EaseOutBounce,

    // EaseInOut
    EaseInOutSine,
    EaseInOutQuad,
    EaseInOutCubic,
    EaseInOutQuart,
    EaseInOutQuint,
    EaseInOutExpo,
    EaseInOutCirc,
    EaseInOutBounce,
}

// イージング関数。C++側の EasedValue を呼び、イージング済みの t を返す。
public static class Easing {

    // 指定タイプでtをイージングした値を返す。
    public static float Evaluate(EasingType type, float t) {
        return NativeApplicationAPI.ReadEasedValue((int)type, t);
    }
}
