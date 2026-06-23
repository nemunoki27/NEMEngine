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

//============================================================================
//	Vector3 structure
//============================================================================
[StructLayout(LayoutKind.Sequential)]
public struct Vector3 {

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
    public readonly float length => Math.Sqrt(x * x + y * y + z * z);
    // 正規化済みベクトル
    public readonly Vector3 normalized => Normalize(this);

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
    public static float Length(Vector3 value) => value.length;

    // ベクトルを正規化する
    public static Vector3 Normalize(Vector3 value) {
        // 0除算を避けるため、十分小さい値は0ベクトルとして扱う
        float len = Length(value);
        return len <= 0.001f ? zero : value / len;
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
    public static Vector3 Lerp(Vector3 lhs, Vector3 rhs, float t) => new(Math.Lerp(lhs.x, rhs.x, t), Math.Lerp(lhs.y, rhs.y, t), Math.Lerp(lhs.z, rhs.z, t));

    // 成分ごとの補間率で線形補間
    public static Vector3 Lerp(Vector3 lhs, Vector3 rhs, Vector3 t) => new(Math.Lerp(lhs.x, rhs.x, t.x), Math.Lerp(lhs.y, rhs.y, t.y), Math.Lerp(lhs.z, rhs.z, t.z));

    // inputをnormalに反射させたベクトルを返す
    public static Vector3 Reflect(Vector3 input, Vector3 normal) => input - normal * (2.0f * Dot(input, normal));

    // 2点間の距離を返す
    public static float Distance(Vector3 lhs, Vector3 rhs) => Length(lhs - rhs);

    // 長さの二乗を返す、平方根を避けたい距離比較用
    public static float SqrMagnitude(Vector3 value) => Dot(value, value);

    // 2ベクトルのなす角(度)を返す
    public static float Angle(Vector3 lhs, Vector3 rhs) {
        float denom = Length(lhs) * Length(rhs);
        if (denom <= 0.001f) {
            return 0.0f;
        }
        return Math.RadToDeg(Math.Acos(Math.Clamp(Dot(lhs, rhs) / denom, -1.0f, 1.0f)));
    }

    // 長さがmaxLengthを超えないようにクランプする
    public static Vector3 ClampMagnitude(Vector3 value, float maxLength) {
        float len = Length(value);
        return len > maxLength && len > 0.001f ? value / len * maxLength : value;
    }

    // currentからtargetへmaxDistanceDeltaを上限に近づける
    public static Vector3 MoveTowards(Vector3 current, Vector3 target, float maxDistanceDelta) {
        Vector3 diff = target - current;
        float dist = Length(diff);
        if (dist <= maxDistanceDelta || dist <= 0.001f) {
            return target;
        }
        return current + diff / dist * maxDistanceDelta;
    }

    // 近似比較
    public static bool NearlyEqual(Vector3 lhs, Vector3 rhs) => Math.NearlyEqual(lhs.x, rhs.x) && Math.NearlyEqual(lhs.y, rhs.y) && Math.NearlyEqual(lhs.z, rhs.z);

    // アングルを参照角度に最も近い360度系へ寄せる
    public static Vector3 MakeContinuousDegrees(Vector3 rawEuler, Vector3 referenceEuler) => new(
        Math.MakeContinuousAngleDegrees(rawEuler.x, referenceEuler.x),
        Math.MakeContinuousAngleDegrees(rawEuler.y, referenceEuler.y),
        Math.MakeContinuousAngleDegrees(rawEuler.z, referenceEuler.z)
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
    public static Vector3 Slerp(Vector3 lhs, Vector3 rhs, float t) {
        float lenL = Length(lhs);
        float lenR = Length(rhs);
        // どちらかが0なら球面補間できないので線形補間へ退避する
        if (lenL < 1e-6f || lenR < 1e-6f) {
            return Lerp(lhs, rhs, t);
        }
        Vector3 dirL = lhs / lenL;
        Vector3 dirR = rhs / lenR;
        float dot = Math.Clamp(Dot(dirL, dirR), -1.0f, 1.0f);
        float theta = Math.Acos(dot) * t;
        Vector3 relative = Normalize(dirR - dirL * dot);
        Vector3 dir = dirL * Math.Cos(theta) + relative * Math.Sin(theta);
        return dir * Math.Lerp(lenL, lenR, t);
    }

    // 減衰しながらtargetへ滑らかに近づける、currentVelocityは呼び出し側で保持する
    public static Vector3 SmoothDamp(Vector3 current, Vector3 target, ref Vector3 currentVelocity,
        float smoothTime, float deltaTime, float maxSpeed = Math.infinity) {

        // 臨界減衰ばねによる追従、Game Programming Gems 4 の式
        smoothTime = Math.Max(0.0001f, smoothTime);
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
            change = change / Math.Sqrt(sqrMag) * maxChange;
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
        => SmoothDamp(current, target, ref currentVelocity, smoothTime, Time.DeltaTime);

    public override readonly string ToString() => $"({x}, {y}, {z})";
}

//============================================================================
//	Vector4 structure
//============================================================================
[StructLayout(LayoutKind.Sequential)]
public struct Vector4 {

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
    public readonly float length => Math.Sqrt(x * x + y * y + z * z + w * w);

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

    public override readonly string ToString() => $"({x}, {y}, {z}, {w})";
}

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

//============================================================================
//	Color3 structure
//============================================================================
[StructLayout(LayoutKind.Sequential)]
public struct Color3 {

    // 赤成分
    public float r;
    // 緑成分
    public float g;
    // 青成分
    public float b;

    public Color3(float r, float g, float b) {
        this.r = r;
        this.g = g;
        this.b = b;
    }

    // 定数色
    public static Color3 black => new(0.0f, 0.0f, 0.0f);
    public static Color3 white => new(1.0f, 1.0f, 1.0f);
    public static Color3 red => new(1.0f, 0.0f, 0.0f);
    public static Color3 green => new(0.0f, 1.0f, 0.0f);
    public static Color3 blue => new(0.0f, 0.0f, 1.0f);

    //--------- operators ----------------------------------------------------

    public static Color3 operator +(Color3 lhs, Color3 rhs) => new(lhs.r + rhs.r, lhs.g + rhs.g, lhs.b + rhs.b);
    public static Color3 operator -(Color3 lhs, Color3 rhs) => new(lhs.r - rhs.r, lhs.g - rhs.g, lhs.b - rhs.b);
    public static Color3 operator *(Color3 lhs, Color3 rhs) => new(lhs.r * rhs.r, lhs.g * rhs.g, lhs.b * rhs.b);
    public static Color3 operator /(Color3 lhs, Color3 rhs) => new(lhs.r / rhs.r, lhs.g / rhs.g, lhs.b / rhs.b);
    public static Color3 operator *(Color3 lhs, float rhs) => new(lhs.r * rhs, lhs.g * rhs, lhs.b * rhs);
    public static Color3 operator /(Color3 lhs, float rhs) => new(lhs.r / rhs, lhs.g / rhs, lhs.b / rhs);

    //--------- functions ----------------------------------------------------

    // 線形補間
    public static Color3 Lerp(Color3 lhs, Color3 rhs, float t) => new(Math.Lerp(lhs.r, rhs.r, t), Math.Lerp(lhs.g, rhs.g, t), Math.Lerp(lhs.b, rhs.b, t));

    // 0xRRGGBBAAからRGBだけを取り出す
    public static Color3 FromHex(uint hex) {
        Color4 color = Color4.FromHex(hex);
        return new Color3(color.r, color.g, color.b);
    }

    public override readonly string ToString() => $"({r}, {g}, {b})";
}

//============================================================================
//	Color4 structure
//============================================================================
[StructLayout(LayoutKind.Sequential)]
public struct Color4 {

    // 赤成分
    public float r;
    // 緑成分
    public float g;
    // 青成分
    public float b;
    // 透明度成分
    public float a;

    public Color4(float r, float g, float b, float a = 1.0f) {
        this.r = r;
        this.g = g;
        this.b = b;
        this.a = a;
    }

    // 定数色
    public static Color4 black => new(0.0f, 0.0f, 0.0f, 1.0f);
    public static Color4 white => new(1.0f, 1.0f, 1.0f, 1.0f);
    public static Color4 red => new(1.0f, 0.0f, 0.0f, 1.0f);
    public static Color4 green => new(0.0f, 1.0f, 0.0f, 1.0f);
    public static Color4 blue => new(0.0f, 0.0f, 1.0f, 1.0f);

    //--------- operators ----------------------------------------------------

    public static Color4 operator +(Color4 lhs, Color4 rhs) => new(lhs.r + rhs.r, lhs.g + rhs.g, lhs.b + rhs.b, lhs.a + rhs.a);
    public static Color4 operator -(Color4 lhs, Color4 rhs) => new(lhs.r - rhs.r, lhs.g - rhs.g, lhs.b - rhs.b, lhs.a - rhs.a);
    public static Color4 operator *(Color4 lhs, Color4 rhs) => new(lhs.r * rhs.r, lhs.g * rhs.g, lhs.b * rhs.b, lhs.a * rhs.a);
    public static Color4 operator /(Color4 lhs, Color4 rhs) => new(lhs.r / rhs.r, lhs.g / rhs.g, lhs.b / rhs.b, lhs.a / rhs.a);
    public static Color4 operator *(Color4 lhs, float rhs) => new(lhs.r * rhs, lhs.g * rhs, lhs.b * rhs, lhs.a * rhs);
    public static Color4 operator /(Color4 lhs, float rhs) => new(lhs.r / rhs, lhs.g / rhs, lhs.b / rhs, lhs.a / rhs);

    //--------- functions ----------------------------------------------------

    // 線形補間
    public static Color4 Lerp(Color4 lhs, Color4 rhs, float t) => new(Math.Lerp(lhs.r, rhs.r, t), Math.Lerp(lhs.g, rhs.g, t), Math.Lerp(lhs.b, rhs.b, t), Math.Lerp(lhs.a, rhs.a, t));

    // 0xRRGGBBAAをリニアRGB + alphaに変換する
    public static Color4 FromHex(uint hex) {
        // 入力はsRGBとして扱う
        float sr = ((hex >> 24) & 0xFF) / 255.0f;
        float sg = ((hex >> 16) & 0xFF) / 255.0f;
        float sb = ((hex >> 8) & 0xFF) / 255.0f;
        float sa = (hex & 0xFF) / 255.0f;
        return new Color4(SRGBToLinear(sr), SRGBToLinear(sg), SRGBToLinear(sb), sa);
    }

    // sRGBからリニアRGBに変換する
    public static float SRGBToLinear(float value) => value <= 0.04045f ? value / 12.92f : Math.Pow((value + 0.055f) / 1.055f, 2.4f);

    public override readonly string ToString() => $"({r}, {g}, {b}, {a})";
}

//============================================================================
//	Math class
//============================================================================
public static class Math {

    // 円周率
    public const float pi = System.MathF.PI;
    // degreeからradianへ変換する係数
    public const float radian = pi / 180.0f;
    // 正の無限大
    public const float infinity = float.PositiveInfinity;
    // 表現可能な最小の正の値
    public const float epsilon = float.Epsilon;

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
    public static float Lerp(float lhs, float rhs, float t) => lhs + (rhs - lhs) * t;

    // 0..1へ収める
    public static float Clamp01(float value) => System.Math.Clamp(value, 0.0f, 1.0f);

    // 値の符号を1か-1で返す
    public static float Sign(float value) => value >= 0.0f ? 1.0f : -1.0f;

    public static float Floor(float value) => System.MathF.Floor(value);
    public static float Ceil(float value) => System.MathF.Ceiling(value);
    public static float Round(float value) => System.MathF.Round(value);
    public static float Exp(float value) => System.MathF.Exp(value);
    public static float Log(float value) => System.MathF.Log(value);

    // lhs..rhs間でのvalueの正規化位置を返す
    public static float InverseLerp(float lhs, float rhs, float value) => lhs == rhs ? 0.0f : Clamp01((value - lhs) / (rhs - lhs));

    // currentからtargetへmaxDeltaを上限に近づける
    public static float MoveTowards(float current, float target, float maxDelta) {
        if (Abs(target - current) <= maxDelta) {
            return target;
        }
        return current + Sign(target - current) * maxDelta;
    }

    // 0..lengthでループした値を返す
    public static float Repeat(float t, float length) => Clamp(t - Floor(t / length) * length, 0.0f, length);

    // 0..lengthを往復した値を返す
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

    // 2角度の最短差分(度)を返す
    public static float DeltaAngle(float current, float target) {
        float delta = Repeat(target - current, 360.0f);
        if (delta > 180.0f) {
            delta -= 360.0f;
        }
        return delta;
    }

    // 角度(度)を最短経路で補間する
    public static float LerpAngle(float lhs, float rhs, float t) => lhs + DeltaAngle(lhs, rhs) * t;

    // 近似比較のUnity互換エイリアス
    public static bool Approximately(float lhs, float rhs) => NearlyEqual(lhs, rhs);

    // ラジアンから度に変換する
    public static float RadToDeg(float rad) => rad * (180.0f / pi);
    public static Vector2 RadToDeg(Vector2 rad) => new(RadToDeg(rad.x), RadToDeg(rad.y));
    public static Vector3 RadToDeg(Vector3 rad) => new(RadToDeg(rad.x), RadToDeg(rad.y), RadToDeg(rad.z));
    public static Vector4 RadToDeg(Vector4 rad) => new(RadToDeg(rad.x), RadToDeg(rad.y), RadToDeg(rad.z), RadToDeg(rad.w));

    // 度からラジアンに変換する
    public static float DegToRad(float deg) => deg * radian;
    public static Vector2 DegToRad(Vector2 deg) => new(DegToRad(deg.x), DegToRad(deg.y));
    public static Vector3 DegToRad(Vector3 deg) => new(DegToRad(deg.x), DegToRad(deg.y), DegToRad(deg.z));
    public static Vector4 DegToRad(Vector4 deg) => new(DegToRad(deg.x), DegToRad(deg.y), DegToRad(deg.z), DegToRad(deg.w));

    // [0, 360)へ丸める
    public static float WrapDegree360(float value) => WrapRange(value, 0.0f, 360.0f);

    // [-180, 180]付近へ丸める
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

    // rawAngleをreferenceAngleから見て最も近い角度表現へ寄せる
    public static float MakeContinuousAngleDegrees(float rawAngle, float referenceAngle) => referenceAngle + System.MathF.IEEERemainder(rawAngle - referenceAngle, 360.0f);

    // 近似比較
    public static bool NearlyEqual(float lhs, float rhs) => Abs(lhs - rhs) <= 0.001f;

    // 値を[minValue, maxValue)の範囲に収める
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
