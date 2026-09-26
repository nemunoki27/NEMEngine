using System.Runtime.InteropServices;

namespace NEMEngine;

//============================================================================
//	Vector4 structure
//============================================================================
[StructLayout(LayoutKind.Sequential)]
public struct Vector4 : IEquatable<Vector4> {

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
    public readonly float magnitude => Mathf.Sqrt(x * x + y * y + z * z + w * w);

    // 平方根を取らない長さ
    public readonly float sqrMagnitude => x * x + y * y + z * z + w * w;

    public readonly Vector4 normalized => Normalize(this);
    public static float Dot(Vector4 lhs, Vector4 rhs) => lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
    public static float Magnitude(Vector4 value) => value.magnitude;
    public static Vector4 Normalize(Vector4 value) => value.magnitude > 1e-5f ? value / value.magnitude : zero;
    public static Vector4 Lerp(Vector4 lhs, Vector4 rhs, float t) => LerpUnclamped(lhs, rhs, Mathf.Clamp01(t));
    public static Vector4 LerpUnclamped(Vector4 lhs, Vector4 rhs, float t) => lhs + (rhs - lhs) * t;

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

    public void Normalize() { this = Normalize(this); }

    // 演算子は近似、Equalsは成分の一致で比較する
    public static bool operator ==(Vector4 lhs, Vector4 rhs) => (lhs - rhs).sqrMagnitude < 1e-10f;
    public static bool operator !=(Vector4 lhs, Vector4 rhs) => !(lhs == rhs);
    public readonly bool Equals(Vector4 other) => x.Equals(other.x) && y.Equals(other.y) && z.Equals(other.z) && w.Equals(other.w);
    public override readonly bool Equals(object? other) => other is Vector4 value && Equals(value);
    public override readonly int GetHashCode() => HashCode.Combine(x, y, z, w);

    public override readonly string ToString() => $"({x}, {y}, {z}, {w})";
}
