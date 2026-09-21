using System.Runtime.InteropServices;

namespace NEMEngine;

//============================================================================
//	Vector4 structure
//============================================================================
[StructLayout(LayoutKind.Sequential)]
public struct Vector4 {

    // X成分
    public float x;
    // Y成分
    public float y;
    // Z成分
    public float z;
    // W成分
    public float w;

    public Vector4(float x, float y, float z, float w) {
        this.x = x;
        this.y = y;
        this.z = z;
        this.w = w;
    }

    // すべての成分が0のベクトル
    public static Vector4 zero => new(0.0f, 0.0f, 0.0f, 0.0f);
    // すべての成分が1のベクトル
    public static Vector4 one => new(1.0f, 1.0f, 1.0f, 1.0f);

    // ベクトルの長さ
    public readonly float length => Math.Sqrt(x * x + y * y + z * z + w * w);

    //--------- operators ----------------------------------------------------

    public static Vector4 operator +(Vector4 lhs, Vector4 rhs) => new(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w);
    public static Vector4 operator -(Vector4 lhs, Vector4 rhs) => new(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z, lhs.w - rhs.w);
    public static Vector4 operator *(Vector4 lhs, Vector4 rhs) => new(lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z, lhs.w * rhs.w);
    public static Vector4 operator /(Vector4 lhs, Vector4 rhs) => new(lhs.x / rhs.x, lhs.y / rhs.y, lhs.z / rhs.z, lhs.w / rhs.w);
    public static Vector4 operator +(Vector4 lhs, float rhs) => new(lhs.x + rhs, lhs.y + rhs, lhs.z + rhs, lhs.w + rhs);
    public static Vector4 operator -(Vector4 lhs, float rhs) => new(lhs.x - rhs, lhs.y - rhs, lhs.z - rhs, lhs.w - rhs);
    public static Vector4 operator *(Vector4 lhs, float rhs) => new(lhs.x * rhs, lhs.y * rhs, lhs.z * rhs, lhs.w * rhs);
    public static Vector4 operator /(Vector4 lhs, float rhs) => new(lhs.x / rhs, lhs.y / rhs, lhs.z / rhs, lhs.w / rhs);
    public static Vector4 operator -(Vector4 value) => new(-value.x, -value.y, -value.z, -value.w);

    public override readonly string ToString() => $"({x}, {y}, {z}, {w})";
}
