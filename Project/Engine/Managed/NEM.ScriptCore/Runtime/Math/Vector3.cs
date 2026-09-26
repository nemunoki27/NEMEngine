using System.Runtime.InteropServices;

namespace NEMEngine;

//============================================================================
//	Vector3 structure
//============================================================================
[StructLayout(LayoutKind.Sequential)]
public struct Vector3 : IEquatable<Vector3> {

    // X成分
    public float x;
    // Y成分
    public float y;
    // Z成分
    public float z;

    public Vector3(float x, float y, float z) {
        this.x = x;
        this.y = y;
        this.z = z;
    }

    // すべての成分が0のベクトル
    public static Vector3 zero => new(0.0f, 0.0f, 0.0f);
    // すべての成分が1のベクトル
    public static Vector3 one => new(1.0f, 1.0f, 1.0f);

    // 方向定数、左手座標系で+Zが前方
    public static Vector3 forward => new(0.0f, 0.0f, 1.0f);
    public static Vector3 back => new(0.0f, 0.0f, -1.0f);
    public static Vector3 up => new(0.0f, 1.0f, 0.0f);
    public static Vector3 down => new(0.0f, -1.0f, 0.0f);
    public static Vector3 right => new(1.0f, 0.0f, 0.0f);
    public static Vector3 left => new(-1.0f, 0.0f, 0.0f);

    // ベクトルの長さ
    public readonly float magnitude => Mathf.Sqrt(x * x + y * y + z * z);
    // 正規化済みベクトル
    public readonly Vector3 normalized => Normalize(this);

    // 平方根を取らない長さ
    public readonly float sqrMagnitude => x * x + y * y + z * z;

    //--------- operators ----------------------------------------------------

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

    //--------- functions ----------------------------------------------------

    // ベクトルの長さを返す
    public static float Magnitude(Vector3 value) => value.magnitude;

    // ベクトルを正規化する
    public static Vector3 Normalize(Vector3 value) {
        // 0除算を避けるため、十分小さい値は0ベクトルとして扱う
        float len = Magnitude(value);
        return len <= 1e-5f ? zero : value / len;
    }

    // 内積を返す
    public static float Dot(Vector3 lhs, Vector3 rhs) => lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;

    // 外積を返す
    public static Vector3 Cross(Vector3 lhs, Vector3 rhs) => new(
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x
    );

    // 線形補間
    public static Vector3 Lerp(Vector3 lhs, Vector3 rhs, float t) => new(Mathf.Lerp(lhs.x, rhs.x, t), Mathf.Lerp(lhs.y, rhs.y, t), Mathf.Lerp(lhs.z, rhs.z, t));

    // 成分ごとの補間率で線形補間
    public static Vector3 Lerp(Vector3 lhs, Vector3 rhs, Vector3 t) => new(Mathf.Lerp(lhs.x, rhs.x, t.x), Mathf.Lerp(lhs.y, rhs.y, t.y), Mathf.Lerp(lhs.z, rhs.z, t.z));

    // inputをnormalに反射させたベクトルを返す
    public static Vector3 Reflect(Vector3 input, Vector3 normal) => input - normal * (2.0f * Dot(input, normal));

    // 2点間の距離を返す
    public static float Distance(Vector3 lhs, Vector3 rhs) => Magnitude(lhs - rhs);

    // 長さの二乗を返す、平方根を避けたい距離比較用
    public static float SqrMagnitude(Vector3 value) => Dot(value, value);

    // 2ベクトルのなす角(度)を返す
    public static float Angle(Vector3 lhs, Vector3 rhs) {
        float denom = Magnitude(lhs) * Magnitude(rhs);
        if (denom < 1e-15f) {
            return 0.0f;
        }
        return Mathf.RadToDeg(Mathf.Acos(Mathf.Clamp(Dot(lhs, rhs) / denom, -1.0f, 1.0f)));
    }

    // 長さがmaxLengthを超えないようにクランプする
    public static Vector3 ClampMagnitude(Vector3 value, float maxLength) {
        float len = Magnitude(value);
        return len > maxLength && len > 0.0f ? value / len * maxLength : value;
    }

    // currentからtargetへmaxDistanceDeltaを上限に近づける
    public static Vector3 MoveTowards(Vector3 current, Vector3 target, float maxDistanceDelta) {
        Vector3 diff = target - current;
        float dist = Magnitude(diff);
        if (dist <= maxDistanceDelta || dist == 0.0f) {
            return target;
        }
        return current + diff / dist * maxDistanceDelta;
    }

    // 範囲外へ外挿する線形補間
    public static Vector3 LerpUnclamped(Vector3 lhs, Vector3 rhs, float t) => lhs + (rhs - lhs) * t;

    // 近似比較
    public static bool NearlyEqual(Vector3 lhs, Vector3 rhs) => Mathf.NearlyEqual(lhs.x, rhs.x) && Mathf.NearlyEqual(lhs.y, rhs.y) && Mathf.NearlyEqual(lhs.z, rhs.z);

    // アングルを参照角度に最も近い360度系へ寄せる
    public static Vector3 MakeContinuousDegrees(Vector3 rawEuler, Vector3 referenceEuler) => new(
        Mathf.MakeContinuousAngleDegrees(rawEuler.x, referenceEuler.x),
        Mathf.MakeContinuousAngleDegrees(rawEuler.y, referenceEuler.y),
        Mathf.MakeContinuousAngleDegrees(rawEuler.z, referenceEuler.z)
    );

    // onNormal方向への射影
    public static Vector3 Project(Vector3 value, Vector3 onNormal) {
        float sqr = Dot(onNormal, onNormal);
        return sqr < 1e-12f ? zero : onNormal * (Dot(value, onNormal) / sqr);
    }

    // planeNormalを法線とする平面への射影、normal成分を取り除く
    public static Vector3 ProjectOnPlane(Vector3 value, Vector3 planeNormal) => value - Project(value, planeNormal);

    // axis周りで測ったfromからtoへの符号付き角度(度)
    public static float SignedAngle(Vector3 from, Vector3 to, Vector3 axis) {
        float sign = Dot(axis, Cross(from, to)) < 0.0f ? -1.0f : 1.0f;
        return Angle(from, to) * sign;
    }

    // 球面線形補間、向きと長さを別々に補間する
    public static Vector3 Slerp(Vector3 lhs, Vector3 rhs, float t) => SlerpUnclamped(lhs, rhs, Mathf.Clamp01(t));

    public static Vector3 SlerpUnclamped(Vector3 lhs, Vector3 rhs, float t) {
        float lenL = Magnitude(lhs);
        float lenR = Magnitude(rhs);
        // どちらかが0なら球面補間できないので線形補間へ退避する
        if (lenL < 1e-6f || lenR < 1e-6f) {
            return LerpUnclamped(lhs, rhs, t);
        }
        Vector3 dirL = lhs / lenL;
        Vector3 dirR = rhs / lenR;
        float dot = Mathf.Clamp(Dot(dirL, dirR), -1.0f, 1.0f);
        float theta = Mathf.Acos(dot) * t;
        Vector3 relative = Normalize(dirR - dirL * dot);
        // 逆向きでは補間用の垂直方向を選ぶ
        if (relative.sqrMagnitude < 1e-10f && dot < 0.0f) {
            relative = Normalize(Cross(dirL, Mathf.Abs(dirL.x) < 0.9f ? right : up));
        }
        Vector3 dir = dirL * Mathf.Cos(theta) + relative * Mathf.Sin(theta);
        return dir * Mathf.LerpUnclamped(lenL, lenR, t);
    }

    // 減衰しながらtargetへ滑らかに近づける、currentVelocityは呼び出し側で保持する
    public static Vector3 SmoothDamp(Vector3 current, Vector3 target, ref Vector3 currentVelocity,
        float smoothTime, float maxSpeed, float deltaTime) {

        // 臨界減衰ばねによる追従、Game Programming Gems 4 の式
        smoothTime = Mathf.Max(0.0001f, smoothTime);
        float omega = 2.0f / smoothTime;
        float x = omega * deltaTime;
        float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

        Vector3 change = current - target;
        Vector3 originalTo = target;
        // 速度上限を移動量へ換算してクランプする
        float maxChange = maxSpeed * smoothTime;
        float maxChangeSq = maxChange * maxChange;
        float sqrMag = Dot(change, change);
        if (sqrMag > maxChangeSq && sqrMag > 0.0f) {
            change = change / Mathf.Sqrt(sqrMag) * maxChange;
        }
        target = current - change;

        Vector3 temp = (currentVelocity + change * omega) * deltaTime;
        currentVelocity = (currentVelocity - temp * omega) * exp;
        Vector3 output = target + (change + temp) * exp;

        // 目標を行き過ぎたら張り付かせて振動を防ぐ
        if (Dot(originalTo - current, output - originalTo) > 0.0f) {
            output = originalTo;
            currentVelocity = (output - originalTo) / deltaTime;
        }
        return output;
    }

    // deltaTime省略版、フレーム間秒数を自動で使う
    public static Vector3 SmoothDamp(Vector3 current, Vector3 target, ref Vector3 currentVelocity, float smoothTime)
        => SmoothDamp(current, target, ref currentVelocity, smoothTime, Mathf.Infinity, Time.deltaTime);

    public static Vector3 SmoothDamp(Vector3 current, Vector3 target, ref Vector3 currentVelocity, float smoothTime, float maxSpeed)
        => SmoothDamp(current, target, ref currentVelocity, smoothTime, maxSpeed, Time.deltaTime);

    public void Normalize() { this = Normalize(this); }

    // 演算子は近似、Equalsは成分の一致で比較する
    public static bool operator ==(Vector3 lhs, Vector3 rhs) => (lhs - rhs).sqrMagnitude < 1e-10f;
    public static bool operator !=(Vector3 lhs, Vector3 rhs) => !(lhs == rhs);
    public readonly bool Equals(Vector3 other) => x.Equals(other.x) && y.Equals(other.y) && z.Equals(other.z);
    public override readonly bool Equals(object? other) => other is Vector3 value && Equals(value);
    public override readonly int GetHashCode() => HashCode.Combine(x, y, z);

    public override readonly string ToString() => $"({x}, {y}, {z})";
}
