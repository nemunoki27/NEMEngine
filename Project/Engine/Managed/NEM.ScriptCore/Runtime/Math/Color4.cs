using System.Runtime.InteropServices;

namespace NEMEngine;

//============================================================================
//	Color4 structure
//============================================================================
[StructLayout(LayoutKind.Sequential)]
public struct Color4 {

    // 赤成分
    public float r;
    // 緑成分
    public float g;
    // 青成分
    public float b;
    // 透明度成分
    public float a;

    public Color4(float r, float g, float b, float a = 1.0f) {
        this.r = r;
        this.g = g;
        this.b = b;
        this.a = a;
    }

    // 定数色
    public static Color4 black => new(0.0f, 0.0f, 0.0f, 1.0f);
    public static Color4 white => new(1.0f, 1.0f, 1.0f, 1.0f);
    public static Color4 red => new(1.0f, 0.0f, 0.0f, 1.0f);
    public static Color4 green => new(0.0f, 1.0f, 0.0f, 1.0f);
    public static Color4 blue => new(0.0f, 0.0f, 1.0f, 1.0f);

    //--------- operators ----------------------------------------------------

    public static Color4 operator +(Color4 lhs, Color4 rhs) => new(lhs.r + rhs.r, lhs.g + rhs.g, lhs.b + rhs.b, lhs.a + rhs.a);
    public static Color4 operator -(Color4 lhs, Color4 rhs) => new(lhs.r - rhs.r, lhs.g - rhs.g, lhs.b - rhs.b, lhs.a - rhs.a);
    public static Color4 operator *(Color4 lhs, Color4 rhs) => new(lhs.r * rhs.r, lhs.g * rhs.g, lhs.b * rhs.b, lhs.a * rhs.a);
    public static Color4 operator /(Color4 lhs, Color4 rhs) => new(lhs.r / rhs.r, lhs.g / rhs.g, lhs.b / rhs.b, lhs.a / rhs.a);
    public static Color4 operator *(Color4 lhs, float rhs) => new(lhs.r * rhs, lhs.g * rhs, lhs.b * rhs, lhs.a * rhs);
    public static Color4 operator /(Color4 lhs, float rhs) => new(lhs.r / rhs, lhs.g / rhs, lhs.b / rhs, lhs.a / rhs);

    //--------- functions ----------------------------------------------------

    // 線形補間
    public static Color4 Lerp(Color4 lhs, Color4 rhs, float t) => new(Mathf.Lerp(lhs.r, rhs.r, t), Mathf.Lerp(lhs.g, rhs.g, t), Mathf.Lerp(lhs.b, rhs.b, t), Mathf.Lerp(lhs.a, rhs.a, t));

    // 0xRRGGBBAAをリニアRGB + alphaに変換する
    public static Color4 FromHex(uint hex) {
        // 入力はsRGBとして扱う
        float sr = ((hex >> 24) & 0xFF) / 255.0f;
        float sg = ((hex >> 16) & 0xFF) / 255.0f;
        float sb = ((hex >> 8) & 0xFF) / 255.0f;
        float sa = (hex & 0xFF) / 255.0f;
        return new Color4(SRGBToLinear(sr), SRGBToLinear(sg), SRGBToLinear(sb), sa);
    }

    // sRGBからリニアRGBに変換する
    public static float SRGBToLinear(float value) => value <= 0.04045f ? value / 12.92f : Mathf.Pow((value + 0.055f) / 1.055f, 2.4f);

    public override readonly string ToString() => $"({r}, {g}, {b}, {a})";
}
