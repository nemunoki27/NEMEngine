using System.Runtime.InteropServices;

namespace NEMEngine;

//============================================================================
//	Quaternion structure
//============================================================================
[StructLayout(LayoutKind.Sequential)]
public struct Quaternion {

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
    public readonly float length => Math.Sqrt(x * x + y * y + z * z + w * w);
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

    //--------- functions ----------------------------------------------------

    // 長さを返す
    public static float Length(Quaternion value) => value.length;

    // 正規化する
    public static Quaternion Normalize(Quaternion value) {
        // 0除算を避けるため、十分小さい値はidentityとして扱う
        float len = Length(value);
        return len <= 0.001f ? identity : value / len;
    }

    // 内積を返す
    public static float Dot(Quaternion lhs, Quaternion rhs) => lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;

    // 共役を返す
    public static Quaternion Conjugate(Quaternion value) => new(-value.x, -value.y, -value.z, value.w);

    // 逆クォータニオンを返す
    public static Quaternion Inverse(Quaternion value) {
        Quaternion conjugate = Conjugate(value);
        float normSq = Length(value) * Length(value);
        return normSq <= 0.001f ? identity : conjugate / normSq;
    }

    // 任意軸回転を作成する
    public static Quaternion MakeAxisAngle(Vector3 axis, float angle) {
        float halfAngle = angle * 0.5f;
        float sinHalfAngle = Math.Sin(halfAngle);
        return new Quaternion(axis.x * sinHalfAngle, axis.y * sinHalfAngle, axis.z * sinHalfAngle, Math.Cos(halfAngle));
    }

    // ラジアンのオイラー角からクォータニオンを作成する
    public static Quaternion FromEulerRadians(Vector3 eulerRadians) {
        // 各軸の半角
        float hx = eulerRadians.x * 0.5f;
        float hy = eulerRadians.y * 0.5f;
        float hz = eulerRadians.z * 0.5f;

        // 半角のsin/cos
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

    // 度数法のオイラー角からクォータニオンを作成する
    public static Quaternion FromEulerDegrees(Vector3 eulerDegrees) => FromEulerRadians(Math.DegToRad(eulerDegrees));

    // NEMEngine側と同じ名前のオイラー変換
    public static Quaternion EulerToQuaternion(Vector3 eulerDegrees) => FromEulerDegrees(eulerDegrees);

    // 球面線形補間
    public static Quaternion Lerp(Quaternion lhs, Quaternion rhs, float t) {
        // qと-qは同じ回転なので、短い方の補間経路を選ぶ
        float dot = Dot(lhs, rhs);
        if (dot < 0.0f) {
            lhs = -lhs;
            dot = -dot;
        }

        // ほぼ同じ向きなら通常の線形補間で十分
        if (dot >= 1.0f - float.Epsilon) {
            return Normalize(lhs * (1.0f - t) + rhs * t);
        }

        // 角度から補間係数を作る
        float theta = Math.Acos(Math.Clamp(dot, -1.0f, 1.0f));
        float sinTheta = Math.Sin(theta);
        float scale0 = Math.Sin((1.0f - t) * theta) / sinTheta;
        float scale1 = Math.Sin(t * theta) / sinTheta;
        return Normalize(lhs * scale0 + rhs * scale1);
    }

    // 球面線形補間、Lerpが既に球面補間のためそのエイリアス
    public static Quaternion Slerp(Quaternion lhs, Quaternion rhs, float t) => Lerp(lhs, rhs, t);

    // 2回転のなす角(度)を返す
    public static float Angle(Quaternion lhs, Quaternion rhs) {
        float dot = Math.Abs(Math.Clamp(Dot(lhs, rhs), -1.0f, 1.0f));
        return Math.RadToDeg(2.0f * Math.Acos(dot));
    }

    // 近似比較
    public static bool NearlyEqual(Quaternion lhs, Quaternion rhs) => 1.0f - 0.001f <= Math.Abs(Dot(lhs, rhs));

    // 度数指定の任意軸回転、Unity互換の引数順(角度,軸)。軸は正規化する
    public static Quaternion AngleAxis(float angleDegrees, Vector3 axis) => MakeAxisAngle(Vector3.Normalize(axis), Math.DegToRad(angleDegrees));

    // 度数法オイラー角からの生成、Unity互換エイリアス
    public static Quaternion Euler(float xDegrees, float yDegrees, float zDegrees) => FromEulerDegrees(new Vector3(xDegrees, yDegrees, zDegrees));
    public static Quaternion Euler(Vector3 eulerDegrees) => FromEulerDegrees(eulerDegrees);

    // forwardを+Zへ、upを基準に向ける回転、ネイティブQuaternion::LookRotationと同一規約(左手系)
    public static Quaternion LookRotation(Vector3 forward, Vector3 up) {
        float forwardLength = Vector3.Length(forward);
        if (forwardLength <= 1e-6f) {
            return identity;
        }
        Vector3 axisZ = forward / forwardLength;

        // 右ベクトルはup×forward、forwardと平行なら別の基準upでやり直す
        Vector3 r = Vector3.Cross(up, axisZ);
        float rightLength = Vector3.Length(r);
        if (rightLength <= 1e-6f) {
            Vector3 fallbackUp = Math.Abs(axisZ.y) < 0.99f ? Vector3.up : Vector3.right;
            r = Vector3.Cross(fallbackUp, axisZ);
            rightLength = Vector3.Length(r);
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
            float s = Math.Sqrt(trace + 1.0f) * 2.0f;
            result = new Quaternion((m12 - m21) / s, (m20 - m02) / s, (m01 - m10) / s, 0.25f * s);
        } else if (m00 > m11 && m00 > m22) {
            float s = Math.Sqrt(1.0f + m00 - m11 - m22) * 2.0f;
            result = new Quaternion(0.25f * s, (m01 + m10) / s, (m20 + m02) / s, (m12 - m21) / s);
        } else if (m11 > m22) {
            float s = Math.Sqrt(1.0f + m11 - m00 - m22) * 2.0f;
            result = new Quaternion((m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m20 - m02) / s);
        } else {
            float s = Math.Sqrt(1.0f + m22 - m00 - m11) * 2.0f;
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
        float dot = Math.Clamp(Vector3.Dot(f, t), -1.0f, 1.0f);
        // ほぼ同方向は回転なし
        if (dot >= 1.0f - 1e-6f) {
            return identity;
        }
        // ほぼ逆方向はfに直交する任意軸で180度回す
        if (dot <= -1.0f + 1e-6f) {
            Vector3 axis = Vector3.Cross(Vector3.right, f);
            if (Vector3.Length(axis) < 1e-6f) {
                axis = Vector3.Cross(Vector3.up, f);
            }
            return MakeAxisAngle(Vector3.Normalize(axis), Math.pi);
        }
        return MakeAxisAngle(Vector3.Normalize(Vector3.Cross(f, t)), Math.Acos(dot));
    }

    // fromからtoへ最大maxDegreesDeltaだけ回す
    public static Quaternion RotateTowards(Quaternion from, Quaternion to, float maxDegreesDelta) {
        float angle = Angle(from, to);
        return angle <= 1e-6f ? to : Slerp(from, to, Math.Clamp01(maxDegreesDelta / angle));
    }

    // 度数法オイラー角へ変換する、表示・デバッグ用。回転計算はQuaternionのまま行うこと
    // ネイティブQuaternion::ToEulerRadiansと同一の抽出順
    public readonly Vector3 eulerAngles => Math.RadToDeg(ToEulerRadians(this));

    // クォータニオンからオイラー角(ラジアン)を抽出する
    private static Vector3 ToEulerRadians(Quaternion quaternion) {
        Quaternion q = Normalize(quaternion);
        // 回転行列の必要要素だけ展開する
        float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, ww = q.w * q.w;
        float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        float m00 = ww + xx - yy - zz;
        float m01 = 2.0f * (xy + wz);
        float m02 = 2.0f * (xz - wy);
        float m11 = ww - xx + yy - zz;
        float m12 = 2.0f * (yz + wx);
        float m21 = 2.0f * (yz - wx);
        float m22 = ww - xx - yy + zz;

        float ay = Math.Asin(Math.Clamp(-m02, -1.0f, 1.0f));
        float cy = Math.Cos(ay);
        // ジンバルロック時はxへ寄せてzを0にする
        return Math.Abs(cy) > 1e-6f
            ? new Vector3(Math.Atan2(m12, m22), ay, Math.Atan2(m01, m00))
            : new Vector3(Math.Atan2(-m21, m11), ay, 0.0f);
    }

    public override readonly string ToString() => $"({x}, {y}, {z}, {w})";
}
