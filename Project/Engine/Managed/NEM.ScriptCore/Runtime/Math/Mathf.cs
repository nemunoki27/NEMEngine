namespace NEMEngine;

//============================================================================
//	Mathf class
//============================================================================
public static class Mathf {

    // 円周率
    public const float PI = System.MathF.PI;
    // 度数からラジアンへ変換する係数
    public const float Deg2Rad = PI / 180.0f;
    // ラジアンから度へ変換する係数
    public const float Rad2Deg = 180.0f / PI;
    // 正の無限大
    public const float Infinity = float.PositiveInfinity;
    // 表現可能な最小の正の値
    public const float Epsilon = float.Epsilon;

    //--------- wrapper ------------------------------------------------------

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

    //--------- functions ----------------------------------------------------

    // 線形補間
    public static float Lerp(float lhs, float rhs, float t) => LerpUnclamped(lhs, rhs, Clamp01(t));

    // 範囲外へ外挿する線形補間
    public static float LerpUnclamped(float lhs, float rhs, float t) => lhs + (rhs - lhs) * t;

    // 0..1へ収める
    public static float Clamp01(float value) => System.Math.Clamp(value, 0.0f, 1.0f);

    // 値の符号を1か-1で返す
    public static float Sign(float value) => value >= 0.0f ? 1.0f : -1.0f;

    public static float Floor(float value) => System.MathF.Floor(value);
    public static float Ceil(float value) => System.MathF.Ceiling(value);
    public static float Round(float value) => System.MathF.Round(value);
    public static float Exp(float value) => System.MathF.Exp(value);
    public static float Log(float value) => System.MathF.Log(value);

    // 範囲内の位置を0から1で返す
    public static float InverseLerp(float lhs, float rhs, float value) =>
        lhs == rhs ? 0.0f : Clamp01((value - lhs) / (rhs - lhs));

    // 指定量を上限に目標へ近づける
    public static float MoveTowards(float current, float target, float maxDelta) {
        if (Abs(target - current) <= maxDelta) {
            return target;
        }
        return current + Sign(target - current) * maxDelta;
    }

    // 指定長で値を繰り返す
    public static float Repeat(float t, float length) => Clamp(t - Floor(t / length) * length, 0.0f, length);

    // ゼロと指定長の間を往復
    public static float PingPong(float t, float length) {
        t = Repeat(t, length * 2.0f);
        return length - Abs(t - length);
    }

    // 0..1のtでなめらかに補間する
    public static float SmoothStep(float lhs, float rhs, float t) {
        t = Clamp01(t);
        t = t * t * (3.0f - 2.0f * t);
        return lhs + (rhs - lhs) * t;
    }

    // 角度の最短差分を度数で返す
    public static float DeltaAngle(float current, float target) {
        float delta = Repeat(target - current, 360.0f);
        if (delta > 180.0f) {
            delta -= 360.0f;
        }
        return delta;
    }

    // 度数の角度を最短経路で補間
    public static float LerpAngle(float lhs, float rhs, float t) => lhs + DeltaAngle(lhs, rhs) * Clamp01(t);

    // 値の大きさに応じて近似比較
    public static bool Approximately(float lhs, float rhs) =>
        Abs(lhs - rhs) < Max(1e-6f * Max(Abs(lhs), Abs(rhs)), Epsilon * 8.0f);

    // ラジアンから度に変換する
    public static float RadToDeg(float rad) => rad * (180.0f / PI);
    public static Vector2 RadToDeg(Vector2 rad) => new(RadToDeg(rad.x), RadToDeg(rad.y));
    public static Vector3 RadToDeg(Vector3 rad) => new(RadToDeg(rad.x), RadToDeg(rad.y), RadToDeg(rad.z));
    public static Vector4 RadToDeg(Vector4 rad) =>
        new(RadToDeg(rad.x), RadToDeg(rad.y), RadToDeg(rad.z), RadToDeg(rad.w));

    // 度からラジアンに変換する
    public static float DegToRad(float deg) => deg * Deg2Rad;
    public static Vector2 DegToRad(Vector2 deg) => new(DegToRad(deg.x), DegToRad(deg.y));
    public static Vector3 DegToRad(Vector3 deg) => new(DegToRad(deg.x), DegToRad(deg.y), DegToRad(deg.z));
    public static Vector4 DegToRad(Vector4 deg) =>
        new(DegToRad(deg.x), DegToRad(deg.y), DegToRad(deg.z), DegToRad(deg.w));

    // 角度を0度以上360度未満へ戻す
    public static float WrapDegree360(float value) => WrapRange(value, 0.0f, 360.0f);

    // 角度を負180度より大きく180度以下へ戻す
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
    public static Vector3 WrapDegree360(Vector3 value) =>
        new(WrapDegree360(value.x), WrapDegree360(value.y), WrapDegree360(value.z));
    public static Vector2 WrapDegree180(Vector2 value) => new(WrapDegree180(value.x), WrapDegree180(value.y));
    public static Vector3 WrapDegree180(Vector3 value) =>
        new(WrapDegree180(value.x), WrapDegree180(value.y), WrapDegree180(value.z));

    // 基準角度に最も近い表現へ戻す
    public static float MakeContinuousAngleDegrees(float rawAngle, float referenceAngle) =>
        referenceAngle + System.MathF.IEEERemainder(rawAngle - referenceAngle, 360.0f);

    // 近似比較
    public static bool NearlyEqual(float lhs, float rhs) => Abs(lhs - rhs) <= 0.001f;

    // 値を下限以上かつ上限未満へ戻す
    private static float WrapRange(float value, float minValue, float maxValue) {
        float range = maxValue - minValue;
        if (range <= 0.0f) {
            return value;
        }
        // 非有限値をそのまま伝える
        if (!float.IsFinite(value)) {
            return value;
        }
        // 大きな角度も剰余で一度に戻す
        float wrapped = (value - minValue) % range;
        if (wrapped < 0.0f) {
            wrapped += range;
        }
        return wrapped < range ? minValue + wrapped : minValue;
    }
}
