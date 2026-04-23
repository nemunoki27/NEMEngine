using System.Runtime.InteropServices;

namespace NEMEngine;

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
}
