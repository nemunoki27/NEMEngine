namespace NEMEngine;

// Unityライクな乱数ユーティリティ。session内で共有する単一の状態を持つ。
public static class Random {

    // 既定はseed自動。InitStateで再現可能なseedへ差し替える
    private static System.Random rng = new();

    // 乱数seedを固定して再現可能にする
    public static void InitState(int seed) {
        rng = new System.Random(seed);
    }

    // 0..1の乱数
    public static float value => (float)rng.NextDouble();

    // minInclusive..maxExclusiveの整数乱数（Unityと同じくmaxは含まない）
    public static int Range(int minInclusive, int maxExclusive) {
        if (maxExclusive <= minInclusive) {
            return minInclusive;
        }
        return rng.Next(minInclusive, maxExclusive);
    }

    // minInclusive..maxInclusiveの実数乱数
    public static float Range(float minInclusive, float maxInclusive) {
        return minInclusive + (maxInclusive - minInclusive) * value;
    }

    // 半径1の円内の一様乱数
    public static Vector2 insideUnitCircle {
        get {
            // 面積を一様にするため半径はsqrtで補正する
            float angle = Range(0.0f, Math.pi * 2.0f);
            float radius = Math.Sqrt(value);
            return new Vector2(Math.Cos(angle) * radius, Math.Sin(angle) * radius);
        }
    }

    // 半径1の球面上の一様乱数
    public static Vector3 onUnitSphere {
        get {
            float z = Range(-1.0f, 1.0f);
            float angle = Range(0.0f, Math.pi * 2.0f);
            float r = Math.Sqrt(Math.Max(0.0f, 1.0f - z * z));
            return new Vector3(Math.Cos(angle) * r, Math.Sin(angle) * r, z);
        }
    }

    // 半径1の球内の一様乱数
    public static Vector3 insideUnitSphere {
        get {
            // 体積を一様にするため半径はcbrtで補正する
            float radius = Math.Pow(value, 1.0f / 3.0f);
            return onUnitSphere * radius;
        }
    }

    // 一様乱数の回転（Shoemakeの一様サンプリング）
    public static Quaternion rotation {
        get {
            float u1 = value;
            float u2 = value;
            float u3 = value;
            float sqrt1MinusU1 = Math.Sqrt(1.0f - u1);
            float sqrtU1 = Math.Sqrt(u1);
            float twoPi = Math.pi * 2.0f;
            return new Quaternion(
                sqrt1MinusU1 * Math.Sin(twoPi * u2),
                sqrt1MinusU1 * Math.Cos(twoPi * u2),
                sqrtU1 * Math.Sin(twoPi * u3),
                sqrtU1 * Math.Cos(twoPi * u3));
        }
    }
}
