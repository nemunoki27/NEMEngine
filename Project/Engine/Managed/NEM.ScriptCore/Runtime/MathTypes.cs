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

    public static Vector2 operator +(Vector2 lhs, Vector2 rhs) {
        return new Vector2(lhs.x + rhs.x, lhs.y + rhs.y);
    }

    public static Vector2 operator -(Vector2 lhs, Vector2 rhs) {
        return new Vector2(lhs.x - rhs.x, lhs.y - rhs.y);
    }

    public static Vector2 operator *(Vector2 lhs, float rhs) {
        return new Vector2(lhs.x * rhs, lhs.y * rhs);
    }

    public override readonly string ToString() {
        return $"({x}, {y})";
    }
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

    public static Vector3 operator +(Vector3 lhs, Vector3 rhs) {
        return new Vector3(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z);
    }

    public static Vector3 operator -(Vector3 lhs, Vector3 rhs) {
        return new Vector3(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z);
    }

    public static Vector3 operator *(Vector3 lhs, float rhs) {
        return new Vector3(lhs.x * rhs, lhs.y * rhs, lhs.z * rhs);
    }

    public override readonly string ToString() {
        return $"({x}, {y}, {z})";
    }
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

    public override readonly string ToString() {
        return $"({x}, {y}, {z}, {w})";
    }
}
