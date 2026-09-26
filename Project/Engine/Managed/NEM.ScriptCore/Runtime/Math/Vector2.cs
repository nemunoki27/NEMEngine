using System.Runtime.InteropServices;

namespace NEMEngine;

//============================================================================
//	Vector2 structure
//============================================================================
[StructLayout(LayoutKind.Sequential)]
public struct Vector2 : IEquatable<Vector2> {

    // X成分
    public float x;
    // Y成分
    public float y;

    public Vector2(float x, float y) {
        this.x = x;
        this.y = y;
    }

    // すべての成分が0のベクトル
    public static Vector2 zero => new(0.0f, 0.0f);
    // すべての成分が1のベクトル
    public static Vector2 one => new(1.0f, 1.0f);

    // ベクトルの長さ
    public readonly float magnitude => Mathf.Sqrt(x * x + y * y);
    // 正規化済みベクトル
    public readonly Vector2 normalized => Normalize(this);

    // 平方根を取らない長さ
    public readonly float sqrMagnitude => x * x + y * y;

    //--------- operators ----------------------------------------------------

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

    //--------- functions ----------------------------------------------------

    // ベクトルの長さを返す
    public static float Magnitude(Vector2 value) => value.magnitude;

    // ベクトルを正規化する
    public static Vector2 Normalize(Vector2 value) {
        // 0除算を避けるため、十分小さい値は0ベクトルとして扱う
        float len = Magnitude(value);
        return len <= 1e-5f ? zero : value / len;
    }

    // 内積を返す
    public static float Dot(Vector2 lhs, Vector2 rhs) => lhs.x * rhs.x + lhs.y * rhs.y;

    // 2Dの外積値をx成分に入れて返す
    public static Vector2 Cross(Vector2 lhs, Vector2 rhs) {
        float cross = lhs.x * rhs.y - lhs.y * rhs.x;
        return new Vector2(cross, 0.0f);
    }

    // 線形補間
    public static Vector2 Lerp(Vector2 lhs, Vector2 rhs, float t) => new(Mathf.Lerp(lhs.x, rhs.x, t), Mathf.Lerp(lhs.y, rhs.y, t));

    // 成分ごとの補間率で線形補間
    public static Vector2 Lerp(Vector2 lhs, Vector2 rhs, Vector2 t) => new(Mathf.Lerp(lhs.x, rhs.x, t.x), Mathf.Lerp(lhs.y, rhs.y, t.y));

    // 2点間の距離を返す
    public static float Distance(Vector2 lhs, Vector2 rhs) => Magnitude(lhs - rhs);

    // 長さの二乗を返す、平方根を避けたい距離比較用
    public static float SqrMagnitude(Vector2 value) => Dot(value, value);

    // 2ベクトルのなす角(度)を返す
    public static float Angle(Vector2 lhs, Vector2 rhs) {
        float denom = Magnitude(lhs) * Magnitude(rhs);
        return denom < 1e-15f ? 0.0f : Mathf.RadToDeg(Mathf.Acos(Mathf.Clamp(Dot(lhs, rhs) / denom, -1.0f, 1.0f)));
    }

    // fromからtoへの符号付き角度(度)、反時計回りが正
    public static float SignedAngle(Vector2 from, Vector2 to) {
        float sign = (from.x * to.y - from.y * to.x) < 0.0f ? -1.0f : 1.0f;
        return Angle(from, to) * sign;
    }

    // 反時計回りに90度回した垂直ベクトル
    public static Vector2 Perpendicular(Vector2 value) => new(-value.y, value.x);

    // currentからtargetへmaxDistanceDeltaを上限に近づける
    public static Vector2 MoveTowards(Vector2 current, Vector2 target, float maxDistanceDelta) {
        Vector2 diff = target - current;
        float dist = Magnitude(diff);
        return (dist <= maxDistanceDelta || dist == 0.0f) ? target : current + diff / dist * maxDistanceDelta;
    }

    // 長さがmaxLengthを超えないようにクランプする
    public static Vector2 ClampMagnitude(Vector2 value, float maxLength) {
        float len = Magnitude(value);
        return len > maxLength && len > 0.0f ? value / len * maxLength : value;
    }

    // 減衰しながらtargetへ滑らかに近づける、currentVelocityは呼び出し側で保持する
    public static Vector2 SmoothDamp(Vector2 current, Vector2 target, ref Vector2 currentVelocity,
        float smoothTime, float maxSpeed, float deltaTime) {

        // 成分ごとにVector3版の臨界減衰ばねを使い回す
        Vector3 vel = new(currentVelocity.x, currentVelocity.y, 0.0f);
        Vector3 result = Vector3.SmoothDamp(new Vector3(current.x, current.y, 0.0f),
            new Vector3(target.x, target.y, 0.0f), ref vel, smoothTime, maxSpeed, deltaTime);
        currentVelocity = new Vector2(vel.x, vel.y);
        return new Vector2(result.x, result.y);
    }

    // deltaTime省略版、フレーム間秒数を自動で使う
    public static Vector2 SmoothDamp(Vector2 current, Vector2 target, ref Vector2 currentVelocity, float smoothTime)
        => SmoothDamp(current, target, ref currentVelocity, smoothTime, Mathf.Infinity, Time.deltaTime);

    public static Vector2 SmoothDamp(Vector2 current, Vector2 target, ref Vector2 currentVelocity, float smoothTime, float maxSpeed)
        => SmoothDamp(current, target, ref currentVelocity, smoothTime, maxSpeed, Time.deltaTime);

    // 範囲外へ外挿する線形補間
    public static Vector2 LerpUnclamped(Vector2 lhs, Vector2 rhs, float t) => lhs + (rhs - lhs) * t;

    public void Normalize() { this = Normalize(this); }

    // 演算子は近似、Equalsは成分の一致で比較する
    public static bool operator ==(Vector2 lhs, Vector2 rhs) => (lhs - rhs).sqrMagnitude < 1e-10f;
    public static bool operator !=(Vector2 lhs, Vector2 rhs) => !(lhs == rhs);
    public readonly bool Equals(Vector2 other) => x.Equals(other.x) && y.Equals(other.y);
    public override readonly bool Equals(object? other) => other is Vector2 value && Equals(value);
    public override readonly int GetHashCode() => HashCode.Combine(x, y);

    public override readonly string ToString() => $"({x}, {y})";
}
