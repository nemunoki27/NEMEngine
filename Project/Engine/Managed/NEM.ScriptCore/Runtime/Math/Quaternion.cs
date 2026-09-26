using System.Runtime.InteropServices;

namespace NEMEngine;

//============================================================================
//	Quaternion structure
//============================================================================
[StructLayout(LayoutKind.Sequential)]
public struct Quaternion : IEquatable<Quaternion> {

    // 虚数部X
    public float x;
    // 虚数部Y
    public float y;
    // 虚数部Z
    public float z;
    // 実数部W
    public float w;

    public Quaternion(float x, float y, float z, float w) {
        this.x = x;
        this.y = y;
        this.z = z;
        this.w = w;
    }

    // 回転なしを表す単位クォータニオン
    public static Quaternion identity => new(0.0f, 0.0f, 0.0f, 1.0f);

    // クォータニオンの長さ
    public readonly float length => Mathf.Sqrt(x * x + y * y + z * z + w * w);
    // 正規化済みクォータニオン
    public readonly Quaternion normalized => Normalize(this);

    //--------- operators ----------------------------------------------------

    public static Quaternion operator +(Quaternion lhs, Quaternion rhs) => new(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w);
    public static Quaternion operator -(Quaternion lhs, Quaternion rhs) => new(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z, lhs.w - rhs.w);
    public static Quaternion operator *(Quaternion lhs, float rhs) => new(lhs.x * rhs, lhs.y * rhs, lhs.z * rhs, lhs.w * rhs);
    public static Quaternion operator /(Quaternion lhs, float rhs) => new(lhs.x / rhs, lhs.y / rhs, lhs.z / rhs, lhs.w / rhs);
    public static Quaternion operator -(Quaternion value) => new(-value.x, -value.y, -value.z, -value.w);

    // クォータニオン同士の積
    public static Quaternion operator *(Quaternion lhs, Quaternion rhs) => new(
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z
    );

    // 回転をベクトルへ適用する
    public static Vector3 operator *(Quaternion rotation, Vector3 point) {
        Vector3 axis = new(rotation.x, rotation.y, rotation.z);
        Vector3 cross = Vector3.Cross(axis, point) * 2.0f;
        return point + cross * rotation.w + Vector3.Cross(axis, cross);
    }

    public static bool operator ==(Quaternion lhs, Quaternion rhs) => Dot(lhs, rhs) > 1.0f - 1e-6f;
    public static bool operator !=(Quaternion lhs, Quaternion rhs) => !(lhs == rhs);
    public readonly bool Equals(Quaternion other) => x.Equals(other.x) && y.Equals(other.y) && z.Equals(other.z) && w.Equals(other.w);
    public override readonly bool Equals(object? other) => other is Quaternion value && Equals(value);
    public override readonly int GetHashCode() => HashCode.Combine(x, y, z, w);
    public void Normalize() { this = Normalize(this); }

    //--------- functions ----------------------------------------------------

    // 長さを返す
    public static float Length(Quaternion value) => value.length;

    // 正規化する
    public static Quaternion Normalize(Quaternion value) {
        // 0除算を避けるため、十分小さい値はidentityとして扱う
        float len = Length(value);
        return len < float.Epsilon ? identity : value / len;
    }

    // 内積を返す
    public static float Dot(Quaternion lhs, Quaternion rhs) => lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;

    // 共役を返す
    public static Quaternion Conjugate(Quaternion value) => new(-value.x, -value.y, -value.z, value.w);

    // 逆クォータニオンを返す
    public static Quaternion Inverse(Quaternion value) {
        Quaternion conjugate = Conjugate(value);
        float normSq = Dot(value, value);
        return normSq < float.Epsilon ? identity : conjugate / normSq;
    }

    // 任意軸回転を作成する
    public static Quaternion MakeAxisAngle(Vector3 axis, float angle) {
        float halfAngle = angle * 0.5f;
        float sinHalfAngle = Mathf.Sin(halfAngle);
        return new Quaternion(axis.x * sinHalfAngle, axis.y * sinHalfAngle, axis.z * sinHalfAngle, Mathf.Cos(halfAngle));
    }

    // ラジアンのオイラー角からクォータニオンを作成する
    public static Quaternion FromEulerRadians(Vector3 eulerRadians) {
        // ZXYで合成する各軸の半角
        float hx = eulerRadians.x * 0.5f;
        float hy = eulerRadians.y * 0.5f;
        float hz = eulerRadians.z * 0.5f;

        // 半角のsin/cos
        float sx = Mathf.Sin(hx);
        float cx = Mathf.Cos(hx);
        float sy = Mathf.Sin(hy);
        float cy = Mathf.Cos(hy);
        float sz = Mathf.Sin(hz);
        float cz = Mathf.Cos(hz);
        return Normalize(new Quaternion(
            sx * cy * cz + cx * sy * sz,
            cx * sy * cz - sx * cy * sz,
            cx * cy * sz - sx * sy * cz,
            cx * cy * cz + sx * sy * sz
        ));
    }

    // 度数法のオイラー角からクォータニオンを作成する
    public static Quaternion FromEulerDegrees(Vector3 eulerDegrees) => FromEulerRadians(Mathf.DegToRad(eulerDegrees));

    // 度数法のオイラー変換
    public static Quaternion EulerToQuaternion(Vector3 eulerDegrees) => FromEulerDegrees(eulerDegrees);

    // 正規化した線形補間
    public static Quaternion Lerp(Quaternion lhs, Quaternion rhs, float t) => LerpUnclamped(lhs, rhs, Mathf.Clamp01(t));

    // 最短経路で線形補間する
    public static Quaternion LerpUnclamped(Quaternion lhs, Quaternion rhs, float t) {
        if (Dot(lhs, rhs) < 0.0f) {
            rhs = -rhs;
        }
        return Normalize(lhs * (1.0f - t) + rhs * t);
    }

    // 球面線形補間
    public static Quaternion Slerp(Quaternion lhs, Quaternion rhs, float t) => SlerpUnclamped(lhs, rhs, Mathf.Clamp01(t));

    // 回転角を保って外挿する
    public static Quaternion SlerpUnclamped(Quaternion lhs, Quaternion rhs, float t) {
        lhs = Normalize(lhs);
        rhs = Normalize(rhs);
        float dot = Dot(lhs, rhs);
        if (dot < 0.0f) {
            rhs = -rhs;
            dot = -dot;
        }
        // 近い回転では小さい正弦による除算を避ける
        if (dot > 0.9995f) {
            return LerpUnclamped(lhs, rhs, t);
        }
        float theta = Mathf.Acos(Mathf.Clamp(dot, -1.0f, 1.0f));
        float sinTheta = Mathf.Sin(theta);
        return Normalize(lhs * (Mathf.Sin((1.0f - t) * theta) / sinTheta) + rhs * (Mathf.Sin(t * theta) / sinTheta));
    }

    // 2回転のなす角(度)を返す
    public static float Angle(Quaternion lhs, Quaternion rhs) {
        float dot = Mathf.Abs(Mathf.Clamp(Dot(lhs, rhs), -1.0f, 1.0f));
        return Mathf.RadToDeg(2.0f * Mathf.Acos(dot));
    }

    // 近似比較
    public static bool NearlyEqual(Quaternion lhs, Quaternion rhs) => 1.0f - 0.001f <= Mathf.Abs(Dot(lhs, rhs));

    // 度数指定の任意軸回転、Unity互換の引数順(角度,軸)。軸は正規化する
    public static Quaternion AngleAxis(float angleDegrees, Vector3 axis) => axis.sqrMagnitude <= 1e-10f
        ? identity : MakeAxisAngle(Vector3.Normalize(axis), Mathf.DegToRad(angleDegrees));

    // 度数法オイラー角からの生成、Unity互換エイリアス
    public static Quaternion Euler(float xDegrees, float yDegrees, float zDegrees) => FromEulerDegrees(new Vector3(xDegrees, yDegrees, zDegrees));
    public static Quaternion Euler(Vector3 eulerDegrees) => FromEulerDegrees(eulerDegrees);

    // forwardを+Zへ、upを基準に向ける回転、ネイティブQuaternion::LookRotationと同一規約(左手系)
    public static Quaternion LookRotation(Vector3 forward, Vector3 up) {
        float forwardLength = Vector3.Magnitude(forward);
        if (forwardLength <= 1e-6f) {
            return identity;
        }
        Vector3 axisZ = forward / forwardLength;

        // 右ベクトルはup×forward、forwardと平行なら別の基準upでやり直す
        Vector3 r = Vector3.Cross(up, axisZ);
        float rightLength = Vector3.Magnitude(r);
        if (rightLength <= 1e-6f) {
            Vector3 fallbackUp = Mathf.Abs(axisZ.y) < 0.99f ? Vector3.up : Vector3.right;
            r = Vector3.Cross(fallbackUp, axisZ);
            rightLength = Vector3.Magnitude(r);
        }
        Vector3 axisX = r / rightLength;
        Vector3 axisY = Vector3.Cross(axisZ, axisX);

        // 各軸を行に並べた回転行列からクォータニオンを復元する
        float m00 = axisX.x, m01 = axisX.y, m02 = axisX.z;
        float m10 = axisY.x, m11 = axisY.y, m12 = axisY.z;
        float m20 = axisZ.x, m21 = axisZ.y, m22 = axisZ.z;

        Quaternion result;
        float trace = m00 + m11 + m22;
        if (trace > 0.0f) {
            float s = Mathf.Sqrt(trace + 1.0f) * 2.0f;
            result = new Quaternion((m12 - m21) / s, (m20 - m02) / s, (m01 - m10) / s, 0.25f * s);
        } else if (m00 > m11 && m00 > m22) {
            float s = Mathf.Sqrt(1.0f + m00 - m11 - m22) * 2.0f;
            result = new Quaternion(0.25f * s, (m01 + m10) / s, (m20 + m02) / s, (m12 - m21) / s);
        } else if (m11 > m22) {
            float s = Mathf.Sqrt(1.0f + m11 - m00 - m22) * 2.0f;
            result = new Quaternion((m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m20 - m02) / s);
        } else {
            float s = Mathf.Sqrt(1.0f + m22 - m00 - m11) * 2.0f;
            result = new Quaternion((m20 + m02) / s, (m12 + m21) / s, 0.25f * s, (m01 - m10) / s);
        }
        return Normalize(result);
    }

    // up省略版、ワールド上方向を基準にする
    public static Quaternion LookRotation(Vector3 forward) => LookRotation(forward, Vector3.up);

    // fromの向きをtoの向きへ合わせる最小回転
    public static Quaternion FromToRotation(Vector3 from, Vector3 to) {
        Vector3 f = Vector3.Normalize(from);
        Vector3 t = Vector3.Normalize(to);
        float dot = Mathf.Clamp(Vector3.Dot(f, t), -1.0f, 1.0f);
        // ほぼ同方向は回転なし
        if (dot >= 1.0f - 1e-6f) {
            return identity;
        }
        // ほぼ逆方向はfに直交する任意軸で180度回す
        if (dot <= -1.0f + 1e-6f) {
            Vector3 axis = Vector3.Cross(Vector3.right, f);
            if (Vector3.Magnitude(axis) < 1e-6f) {
                axis = Vector3.Cross(Vector3.up, f);
            }
            return MakeAxisAngle(Vector3.Normalize(axis), Mathf.PI);
        }
        return MakeAxisAngle(Vector3.Normalize(Vector3.Cross(f, t)), Mathf.Acos(dot));
    }

    // fromからtoへ最大maxDegreesDeltaだけ回す
    public static Quaternion RotateTowards(Quaternion from, Quaternion to, float maxDegreesDelta) {
        float angle = Angle(from, to);
        return angle <= 1e-6f ? to : Slerp(from, to, Mathf.Clamp01(maxDegreesDelta / angle));
    }

    // Z、X、Yの順で回転する度数法のオイラー角
    [System.Text.Json.Serialization.JsonIgnore]
    public Vector3 eulerAngles {
        readonly get => Mathf.WrapDegree360(Mathf.RadToDeg(ToEulerRadians(this)));
        set => this = Euler(value);
    }

    // ZXYの回転行列から角度を復元する
    private static Vector3 ToEulerRadians(Quaternion quaternion) {
        Quaternion q = Normalize(quaternion);
        float m00 = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
        float m01 = 2.0f * (q.x * q.y + q.w * q.z);
        float m02 = 2.0f * (q.x * q.z - q.w * q.y);
        float m11 = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
        float m20 = 2.0f * (q.x * q.z + q.w * q.y);
        float m21 = 2.0f * (q.y * q.z - q.w * q.x);
        float m22 = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
        float ax = Mathf.Asin(Mathf.Clamp(-m21, -1.0f, 1.0f));
        // ジンバルロックではZを0へ寄せる
        return Mathf.Abs(m21) < 0.999999f
            ? new Vector3(ax, Mathf.Atan2(m20, m22), Mathf.Atan2(m01, m11))
            : new Vector3(ax, Mathf.Atan2(-m02, m00), 0.0f);
    }

    public override readonly string ToString() => $"({x}, {y}, {z}, {w})";
}
