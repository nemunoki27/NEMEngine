using System.Runtime.InteropServices;

namespace NEMEngine;

//============================================================================
//	Vector2 structure
//============================================================================
[StructLayout(LayoutKind.Sequential)]
public struct Vector2 {

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
    public readonly float length => Math.Sqrt(x * x + y * y);
    // 正規化済みベクトル
    public readonly Vector2 normalized => Normalize(this);

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
    public static float Length(Vector2 value) => value.length;

    // ベクトルを正規化する
    public static Vector2 Normalize(Vector2 value) {
        // 0除算を避けるため、十分小さい値は0ベクトルとして扱う
        float len = Length(value);
        return len <= 0.001f ? zero : value / len;
    }

    // 内積を返す
    public static float Dot(Vector2 lhs, Vector2 rhs) => lhs.x * rhs.x + lhs.y * rhs.y;

    // 2Dの外積値をx成分に入れて返す
    public static Vector2 Cross(Vector2 lhs, Vector2 rhs) {
        float cross = lhs.x * rhs.y - lhs.y * rhs.x;
        return new Vector2(cross, 0.0f);
    }

    // 線形補間
    public static Vector2 Lerp(Vector2 lhs, Vector2 rhs, float t) => new(Math.Lerp(lhs.x, rhs.x, t), Math.Lerp(lhs.y, rhs.y, t));

    // 成分ごとの補間率で線形補間
    public static Vector2 Lerp(Vector2 lhs, Vector2 rhs, Vector2 t) => new(Math.Lerp(lhs.x, rhs.x, t.x), Math.Lerp(lhs.y, rhs.y, t.y));

    // 2点間の距離を返す
    public static float Distance(Vector2 lhs, Vector2 rhs) => Length(lhs - rhs);

    // 長さの二乗を返す、平方根を避けたい距離比較用
    public static float SqrMagnitude(Vector2 value) => Dot(value, value);

    // 2ベクトルのなす角(度)を返す
    public static float Angle(Vector2 lhs, Vector2 rhs) {
        float denom = Length(lhs) * Length(rhs);
        return denom <= 0.001f ? 0.0f : Math.RadToDeg(Math.Acos(Math.Clamp(Dot(lhs, rhs) / denom, -1.0f, 1.0f)));
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
        float dist = Length(diff);
        return (dist <= maxDistanceDelta || dist <= 0.001f) ? target : current + diff / dist * maxDistanceDelta;
    }

    // 長さがmaxLengthを超えないようにクランプする
    public static Vector2 ClampMagnitude(Vector2 value, float maxLength) {
        float len = Length(value);
        return len > maxLength && len > 0.001f ? value / len * maxLength : value;
    }

    // 減衰しながらtargetへ滑らかに近づける、currentVelocityは呼び出し側で保持する
    public static Vector2 SmoothDamp(Vector2 current, Vector2 target, ref Vector2 currentVelocity,
        float smoothTime, float deltaTime, float maxSpeed = Math.infinity) {

        // 成分ごとにVector3版の臨界減衰ばねを使い回す
        Vector3 vel = new(currentVelocity.x, currentVelocity.y, 0.0f);
        Vector3 result = Vector3.SmoothDamp(new Vector3(current.x, current.y, 0.0f),
            new Vector3(target.x, target.y, 0.0f), ref vel, smoothTime, deltaTime, maxSpeed);
        currentVelocity = new Vector2(vel.x, vel.y);
        return new Vector2(result.x, result.y);
    }

    // deltaTime省略版、フレーム間秒数を自動で使う
    public static Vector2 SmoothDamp(Vector2 current, Vector2 target, ref Vector2 currentVelocity, float smoothTime)
        => SmoothDamp(current, target, ref currentVelocity, smoothTime, Time.DeltaTime);

    public override readonly string ToString() => $"({x}, {y})";
}
