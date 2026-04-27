using System.Runtime.InteropServices;

namespace NEMEngine;

[StructLayout(LayoutKind.Sequential)]
public struct Vector2 {

    public float x;
    public float y;

    public Vector2(float x, float y) {
        this.x = x;
        this.y = y;
    }

    public static Vector2 zero => new(0.0f, 0.0f);
    public static Vector2 one => new(1.0f, 1.0f);

    public readonly float length => Math.Sqrt(x * x + y * y);
    public readonly Vector2 normalized => Normalize(this);

    public static Vector2 operator +(Vector2 lhs, Vector2 rhs) => new(lhs.x + rhs.x, lhs.y + rhs.y);
    public static Vector2 operator -(Vector2 lhs, Vector2 rhs) => new(lhs.x - rhs.x, lhs.y - rhs.y);
    public static Vector2 operator *(Vector2 lhs, Vector2 rhs) => new(lhs.x * rhs.x, lhs.y * rhs.y);
    public static Vector2 operator /(Vector2 lhs, Vector2 rhs) => new(lhs.x / rhs.x, lhs.y / rhs.y);
    public static Vector2 operator +(Vector2 lhs, float rhs) => new(lhs.x + rhs, lhs.y + rhs);
    public static Vector2 operator -(Vector2 lhs, float rhs) => new(lhs.x - rhs, lhs.y - rhs);
    public static Vector2 operator *(Vector2 lhs, float rhs) => new(lhs.x * rhs, lhs.y * rhs);
    public static Vector2 operator /(Vector2 lhs, float rhs) => new(lhs.x / rhs, lhs.y / rhs);
    public static Vector2 operator +(float lhs, Vector2 rhs) => rhs + lhs;
    public static Vector2 operator -(float lhs, Vector2 rhs) => new(lhs - rhs.x, lhs - rhs.y);
    public static Vector2 operator *(float lhs, Vector2 rhs) => rhs * lhs;
    public static Vector2 operator /(float lhs, Vector2 rhs) => new(lhs / rhs.x, lhs / rhs.y);
    public static Vector2 operator -(Vector2 value) => new(-value.x, -value.y);

    public static float Length(Vector2 value) => value.length;
    public static Vector2 Normalize(Vector2 value) {
        float len = Length(value);
        return len <= 0.001f ? zero : value / len;
    }
    public static float Dot(Vector2 lhs, Vector2 rhs) => lhs.x * rhs.x + lhs.y * rhs.y;
    public static Vector2 Cross(Vector2 lhs, Vector2 rhs) {
        float cross = lhs.x * rhs.y - lhs.y * rhs.x;
        return new Vector2(cross, 0.0f);
    }
    public static Vector2 Lerp(Vector2 lhs, Vector2 rhs, float t) => new(Math.Lerp(lhs.x, rhs.x, t), Math.Lerp(lhs.y, rhs.y, t));
    public static Vector2 Lerp(Vector2 lhs, Vector2 rhs, Vector2 t) => new(Math.Lerp(lhs.x, rhs.x, t.x), Math.Lerp(lhs.y, rhs.y, t.y));

    public override readonly string ToString() => $"({x}, {y})";
}

[StructLayout(LayoutKind.Sequential)]
public struct Vector3 {

    public float x;
    public float y;
    public float z;

    public Vector3(float x, float y, float z) {
        this.x = x;
        this.y = y;
        this.z = z;
    }

    public static Vector3 zero => new(0.0f, 0.0f, 0.0f);
    public static Vector3 one => new(1.0f, 1.0f, 1.0f);

    public readonly float length => Math.Sqrt(x * x + y * y + z * z);
    public readonly Vector3 normalized => Normalize(this);

    public static Vector3 operator +(Vector3 lhs, Vector3 rhs) => new(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z);
    public static Vector3 operator -(Vector3 lhs, Vector3 rhs) => new(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z);
    public static Vector3 operator *(Vector3 lhs, Vector3 rhs) => new(lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z);
    public static Vector3 operator /(Vector3 lhs, Vector3 rhs) => new(lhs.x / rhs.x, lhs.y / rhs.y, lhs.z / rhs.z);
    public static Vector3 operator +(Vector3 lhs, float rhs) => new(lhs.x + rhs, lhs.y + rhs, lhs.z + rhs);
    public static Vector3 operator -(Vector3 lhs, float rhs) => new(lhs.x - rhs, lhs.y - rhs, lhs.z - rhs);
    public static Vector3 operator *(Vector3 lhs, float rhs) => new(lhs.x * rhs, lhs.y * rhs, lhs.z * rhs);
    public static Vector3 operator /(Vector3 lhs, float rhs) => new(lhs.x / rhs, lhs.y / rhs, lhs.z / rhs);
    public static Vector3 operator +(float lhs, Vector3 rhs) => rhs + lhs;
    public static Vector3 operator -(float lhs, Vector3 rhs) => new(lhs - rhs.x, lhs - rhs.y, lhs - rhs.z);
    public static Vector3 operator *(float lhs, Vector3 rhs) => rhs * lhs;
    public static Vector3 operator /(float lhs, Vector3 rhs) => new(lhs / rhs.x, lhs / rhs.y, lhs / rhs.z);
    public static Vector3 operator -(Vector3 value) => new(-value.x, -value.y, -value.z);

    public static float Length(Vector3 value) => value.length;
    public static Vector3 Normalize(Vector3 value) {
        float len = Length(value);
        return len <= 0.001f ? zero : value / len;
    }
    public static float Dot(Vector3 lhs, Vector3 rhs) => lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    public static Vector3 Cross(Vector3 lhs, Vector3 rhs) => new(
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x
    );
    public static Vector3 Lerp(Vector3 lhs, Vector3 rhs, float t) => new(Math.Lerp(lhs.x, rhs.x, t), Math.Lerp(lhs.y, rhs.y, t), Math.Lerp(lhs.z, rhs.z, t));
    public static Vector3 Lerp(Vector3 lhs, Vector3 rhs, Vector3 t) => new(Math.Lerp(lhs.x, rhs.x, t.x), Math.Lerp(lhs.y, rhs.y, t.y), Math.Lerp(lhs.z, rhs.z, t.z));
    public static Vector3 Reflect(Vector3 input, Vector3 normal) => input - normal * (2.0f * Dot(input, normal));
    public static bool NearlyEqual(Vector3 lhs, Vector3 rhs) => Math.NearlyEqual(lhs.x, rhs.x) && Math.NearlyEqual(lhs.y, rhs.y) && Math.NearlyEqual(lhs.z, rhs.z);
    public static Vector3 MakeContinuousDegrees(Vector3 rawEuler, Vector3 referenceEuler) => new(
        Math.MakeContinuousAngleDegrees(rawEuler.x, referenceEuler.x),
        Math.MakeContinuousAngleDegrees(rawEuler.y, referenceEuler.y),
        Math.MakeContinuousAngleDegrees(rawEuler.z, referenceEuler.z)
    );

    public override readonly string ToString() => $"({x}, {y}, {z})";
}

[StructLayout(LayoutKind.Sequential)]
public struct Vector4 {

    public float x;
    public float y;
    public float z;
    public float w;

    public Vector4(float x, float y, float z, float w) {
        this.x = x;
        this.y = y;
        this.z = z;
        this.w = w;
    }

    public static Vector4 zero => new(0.0f, 0.0f, 0.0f, 0.0f);
    public static Vector4 one => new(1.0f, 1.0f, 1.0f, 1.0f);

    public readonly float length => Math.Sqrt(x * x + y * y + z * z + w * w);

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

[StructLayout(LayoutKind.Sequential)]
public struct Quaternion {

    public float x;
    public float y;
    public float z;
    public float w;

    public Quaternion(float x, float y, float z, float w) {
        this.x = x;
        this.y = y;
        this.z = z;
        this.w = w;
    }

    public static Quaternion identity => new(0.0f, 0.0f, 0.0f, 1.0f);

    public readonly float length => Math.Sqrt(x * x + y * y + z * z + w * w);
    public readonly Quaternion normalized => Normalize(this);

    public static Quaternion operator +(Quaternion lhs, Quaternion rhs) => new(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w);
    public static Quaternion operator -(Quaternion lhs, Quaternion rhs) => new(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z, lhs.w - rhs.w);
    public static Quaternion operator *(Quaternion lhs, float rhs) => new(lhs.x * rhs, lhs.y * rhs, lhs.z * rhs, lhs.w * rhs);
    public static Quaternion operator /(Quaternion lhs, float rhs) => new(lhs.x / rhs, lhs.y / rhs, lhs.z / rhs, lhs.w / rhs);
    public static Quaternion operator -(Quaternion value) => new(-value.x, -value.y, -value.z, -value.w);
    public static Quaternion operator *(Quaternion lhs, Quaternion rhs) => new(
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z
    );

    public static float Length(Quaternion value) => value.length;
    public static Quaternion Normalize(Quaternion value) {
        float len = Length(value);
        return len <= 0.001f ? identity : value / len;
    }
    public static float Dot(Quaternion lhs, Quaternion rhs) => lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
    public static Quaternion Conjugate(Quaternion value) => new(-value.x, -value.y, -value.z, value.w);
    public static Quaternion Inverse(Quaternion value) {
        Quaternion conjugate = Conjugate(value);
        float normSq = Length(value) * Length(value);
        return normSq <= 0.001f ? identity : conjugate / normSq;
    }
    public static Quaternion MakeAxisAngle(Vector3 axis, float angle) {
        float halfAngle = angle * 0.5f;
        float sinHalfAngle = Math.Sin(halfAngle);
        return new Quaternion(axis.x * sinHalfAngle, axis.y * sinHalfAngle, axis.z * sinHalfAngle, Math.Cos(halfAngle));
    }
    public static Quaternion FromEulerRadians(Vector3 eulerRadians) {
        float hx = eulerRadians.x * 0.5f;
        float hy = eulerRadians.y * 0.5f;
        float hz = eulerRadians.z * 0.5f;
        float sx = Math.Sin(hx);
        float cx = Math.Cos(hx);
        float sy = Math.Sin(hy);
        float cy = Math.Cos(hy);
        float sz = Math.Sin(hz);
        float cz = Math.Cos(hz);
        return Normalize(new Quaternion(
            sx * cy * cz - cx * sy * sz,
            cx * sy * cz + sx * cy * sz,
            cx * cy * sz - sx * sy * cz,
            cx * cy * cz + sx * sy * sz
        ));
    }
    public static Quaternion FromEulerDegrees(Vector3 eulerDegrees) => FromEulerRadians(Math.DegToRad(eulerDegrees));
    public static Quaternion EulerToQuaternion(Vector3 eulerDegrees) => FromEulerDegrees(eulerDegrees);
    public static Quaternion Lerp(Quaternion lhs, Quaternion rhs, float t) {
        float dot = Dot(lhs, rhs);
        if (dot < 0.0f) {
            lhs = -lhs;
            dot = -dot;
        }
        if (dot >= 1.0f - float.Epsilon) {
            return Normalize(lhs * (1.0f - t) + rhs * t);
        }
        float theta = Math.Acos(Math.Clamp(dot, -1.0f, 1.0f));
        float sinTheta = Math.Sin(theta);
        float scale0 = Math.Sin((1.0f - t) * theta) / sinTheta;
        float scale1 = Math.Sin(t * theta) / sinTheta;
        return Normalize(lhs * scale0 + rhs * scale1);
    }
    public static bool NearlyEqual(Quaternion lhs, Quaternion rhs) => 1.0f - 0.001f <= Math.Abs(Dot(lhs, rhs));

    public override readonly string ToString() => $"({x}, {y}, {z}, {w})";
}

[StructLayout(LayoutKind.Sequential)]
public struct Color3 {

    public float r;
    public float g;
    public float b;

    public Color3(float r, float g, float b) {
        this.r = r;
        this.g = g;
        this.b = b;
    }

    public static Color3 black => new(0.0f, 0.0f, 0.0f);
    public static Color3 white => new(1.0f, 1.0f, 1.0f);
    public static Color3 red => new(1.0f, 0.0f, 0.0f);
    public static Color3 green => new(0.0f, 1.0f, 0.0f);
    public static Color3 blue => new(0.0f, 0.0f, 1.0f);

    public static Color3 operator +(Color3 lhs, Color3 rhs) => new(lhs.r + rhs.r, lhs.g + rhs.g, lhs.b + rhs.b);
    public static Color3 operator -(Color3 lhs, Color3 rhs) => new(lhs.r - rhs.r, lhs.g - rhs.g, lhs.b - rhs.b);
    public static Color3 operator *(Color3 lhs, Color3 rhs) => new(lhs.r * rhs.r, lhs.g * rhs.g, lhs.b * rhs.b);
    public static Color3 operator /(Color3 lhs, Color3 rhs) => new(lhs.r / rhs.r, lhs.g / rhs.g, lhs.b / rhs.b);
    public static Color3 operator *(Color3 lhs, float rhs) => new(lhs.r * rhs, lhs.g * rhs, lhs.b * rhs);
    public static Color3 operator /(Color3 lhs, float rhs) => new(lhs.r / rhs, lhs.g / rhs, lhs.b / rhs);

    public static Color3 Lerp(Color3 lhs, Color3 rhs, float t) => new(Math.Lerp(lhs.r, rhs.r, t), Math.Lerp(lhs.g, rhs.g, t), Math.Lerp(lhs.b, rhs.b, t));
    public static Color3 FromHex(uint hex) {
        Color4 color = Color4.FromHex(hex);
        return new Color3(color.r, color.g, color.b);
    }

    public override readonly string ToString() => $"({r}, {g}, {b})";
}

[StructLayout(LayoutKind.Sequential)]
public struct Color4 {

    public float r;
    public float g;
    public float b;
    public float a;

    public Color4(float r, float g, float b, float a = 1.0f) {
        this.r = r;
        this.g = g;
        this.b = b;
        this.a = a;
    }

    public static Color4 black => new(0.0f, 0.0f, 0.0f, 1.0f);
    public static Color4 white => new(1.0f, 1.0f, 1.0f, 1.0f);
    public static Color4 red => new(1.0f, 0.0f, 0.0f, 1.0f);
    public static Color4 green => new(0.0f, 1.0f, 0.0f, 1.0f);
    public static Color4 blue => new(0.0f, 0.0f, 1.0f, 1.0f);

    public static Color4 operator +(Color4 lhs, Color4 rhs) => new(lhs.r + rhs.r, lhs.g + rhs.g, lhs.b + rhs.b, lhs.a + rhs.a);
    public static Color4 operator -(Color4 lhs, Color4 rhs) => new(lhs.r - rhs.r, lhs.g - rhs.g, lhs.b - rhs.b, lhs.a - rhs.a);
    public static Color4 operator *(Color4 lhs, Color4 rhs) => new(lhs.r * rhs.r, lhs.g * rhs.g, lhs.b * rhs.b, lhs.a * rhs.a);
    public static Color4 operator /(Color4 lhs, Color4 rhs) => new(lhs.r / rhs.r, lhs.g / rhs.g, lhs.b / rhs.b, lhs.a / rhs.a);
    public static Color4 operator *(Color4 lhs, float rhs) => new(lhs.r * rhs, lhs.g * rhs, lhs.b * rhs, lhs.a * rhs);
    public static Color4 operator /(Color4 lhs, float rhs) => new(lhs.r / rhs, lhs.g / rhs, lhs.b / rhs, lhs.a / rhs);

    public static Color4 Lerp(Color4 lhs, Color4 rhs, float t) => new(Math.Lerp(lhs.r, rhs.r, t), Math.Lerp(lhs.g, rhs.g, t), Math.Lerp(lhs.b, rhs.b, t), Math.Lerp(lhs.a, rhs.a, t));
    public static Color4 FromHex(uint hex) {
        float sr = ((hex >> 24) & 0xFF) / 255.0f;
        float sg = ((hex >> 16) & 0xFF) / 255.0f;
        float sb = ((hex >> 8) & 0xFF) / 255.0f;
        float sa = (hex & 0xFF) / 255.0f;
        return new Color4(SRGBToLinear(sr), SRGBToLinear(sg), SRGBToLinear(sb), sa);
    }
    public static float SRGBToLinear(float value) => value <= 0.04045f ? value / 12.92f : Math.Pow((value + 0.055f) / 1.055f, 2.4f);

    public override readonly string ToString() => $"({r}, {g}, {b}, {a})";
}

public static class Math {

    public const float pi = System.MathF.PI;
    public const float radian = pi / 180.0f;

    public static float Sin(float value) => System.MathF.Sin(value);
    public static float Cos(float value) => System.MathF.Cos(value);
    public static float Tan(float value) => System.MathF.Tan(value);
    public static float Asin(float value) => System.MathF.Asin(value);
    public static float Acos(float value) => System.MathF.Acos(value);
    public static float Atan2(float y, float x) => System.MathF.Atan2(y, x);
    public static float Sqrt(float value) => System.MathF.Sqrt(value);
    public static float Abs(float value) => System.MathF.Abs(value);
    public static float Pow(float value, float power) => System.MathF.Pow(value, power);
    public static float Clamp(float value, float min, float max) => System.Math.Clamp(value, min, max);
    public static int Min(int lhs, int rhs) => System.Math.Min(lhs, rhs);
    public static int Max(int lhs, int rhs) => System.Math.Max(lhs, rhs);
    public static float Min(float lhs, float rhs) => System.MathF.Min(lhs, rhs);
    public static float Max(float lhs, float rhs) => System.MathF.Max(lhs, rhs);
    public static float Lerp(float lhs, float rhs, float t) => lhs + (rhs - lhs) * t;
    public static float RadToDeg(float rad) => rad * (180.0f / pi);
    public static Vector2 RadToDeg(Vector2 rad) => new(RadToDeg(rad.x), RadToDeg(rad.y));
    public static Vector3 RadToDeg(Vector3 rad) => new(RadToDeg(rad.x), RadToDeg(rad.y), RadToDeg(rad.z));
    public static Vector4 RadToDeg(Vector4 rad) => new(RadToDeg(rad.x), RadToDeg(rad.y), RadToDeg(rad.z), RadToDeg(rad.w));
    public static float DegToRad(float deg) => deg * radian;
    public static Vector2 DegToRad(Vector2 deg) => new(DegToRad(deg.x), DegToRad(deg.y));
    public static Vector3 DegToRad(Vector3 deg) => new(DegToRad(deg.x), DegToRad(deg.y), DegToRad(deg.z));
    public static Vector4 DegToRad(Vector4 deg) => new(DegToRad(deg.x), DegToRad(deg.y), DegToRad(deg.z), DegToRad(deg.w));
    public static float WrapDegree360(float value) => WrapRange(value, 0.0f, 360.0f);
    public static float WrapDegree180(float value) {
        value = System.MathF.IEEERemainder(value, 360.0f);
        if (value <= -180.0f) {
            value += 360.0f;
        }
        if (value > 180.0f) {
            value -= 360.0f;
        }
        return value;
    }
    public static Vector2 WrapDegree360(Vector2 value) => new(WrapDegree360(value.x), WrapDegree360(value.y));
    public static Vector3 WrapDegree360(Vector3 value) => new(WrapDegree360(value.x), WrapDegree360(value.y), WrapDegree360(value.z));
    public static Vector2 WrapDegree180(Vector2 value) => new(WrapDegree180(value.x), WrapDegree180(value.y));
    public static Vector3 WrapDegree180(Vector3 value) => new(WrapDegree180(value.x), WrapDegree180(value.y), WrapDegree180(value.z));
    public static float MakeContinuousAngleDegrees(float rawAngle, float referenceAngle) => referenceAngle + System.MathF.IEEERemainder(rawAngle - referenceAngle, 360.0f);
    public static bool NearlyEqual(float lhs, float rhs) => Abs(lhs - rhs) <= 0.001f;

    private static float WrapRange(float value, float minValue, float maxValue) {
        float range = maxValue - minValue;
        if (range <= 0.0f) {
            return value;
        }
        while (value < minValue) {
            value += range;
        }
        while (value >= maxValue) {
            value -= range;
        }
        return value;
    }
}
