using System.Runtime.InteropServices;

namespace NEMEngine;

//============================================================================
//	Color3 structure
//============================================================================
[StructLayout(LayoutKind.Sequential)]
public struct Color3 {

    // 赤成分
    public float r;
    // 緑成分
    public float g;
    // 青成分
    public float b;

    public Color3(float r, float g, float b) {
        this.r = r;
        this.g = g;
        this.b = b;
    }

    // 定数色
    public static Color3 black => new(0.0f, 0.0f, 0.0f);
    public static Color3 white => new(1.0f, 1.0f, 1.0f);
    public static Color3 red => new(1.0f, 0.0f, 0.0f);
    public static Color3 green => new(0.0f, 1.0f, 0.0f);
    public static Color3 blue => new(0.0f, 0.0f, 1.0f);

    //--------- operators ----------------------------------------------------

    public static Color3 operator +(Color3 lhs, Color3 rhs) => new(lhs.r + rhs.r, lhs.g + rhs.g, lhs.b + rhs.b);
    public static Color3 operator -(Color3 lhs, Color3 rhs) => new(lhs.r - rhs.r, lhs.g - rhs.g, lhs.b - rhs.b);
    public static Color3 operator *(Color3 lhs, Color3 rhs) => new(lhs.r * rhs.r, lhs.g * rhs.g, lhs.b * rhs.b);
    public static Color3 operator /(Color3 lhs, Color3 rhs) => new(lhs.r / rhs.r, lhs.g / rhs.g, lhs.b / rhs.b);
    public static Color3 operator *(Color3 lhs, float rhs) => new(lhs.r * rhs, lhs.g * rhs, lhs.b * rhs);
    public static Color3 operator /(Color3 lhs, float rhs) => new(lhs.r / rhs, lhs.g / rhs, lhs.b / rhs);

    //--------- functions ----------------------------------------------------

    // 線形補間
    public static Color3 Lerp(Color3 lhs, Color3 rhs, float t) => new(Math.Lerp(lhs.r, rhs.r, t), Math.Lerp(lhs.g, rhs.g, t), Math.Lerp(lhs.b, rhs.b, t));

    // 0xRRGGBBAAからRGBだけを取り出す
    public static Color3 FromHex(uint hex) {
        Color4 color = Color4.FromHex(hex);
        return new Color3(color.r, color.g, color.b);
    }

    public override readonly string ToString() => $"({r}, {g}, {b})";
}
