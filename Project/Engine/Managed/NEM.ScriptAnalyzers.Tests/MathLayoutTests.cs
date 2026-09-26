using System.Runtime.InteropServices;
using NEMEngine;

namespace NEM.ScriptAnalyzers.Tests;

//============================================================================
//	MathLayoutTests class
//	数学型のNative配置と基本演算を検証する
//============================================================================
internal static class MathLayoutTests {

	internal static void Run() {

		Check<Vector2>(8, "x", "y");
		Check<Vector3>(12, "x", "y", "z");
		Check<Vector4>(16, "x", "y", "z", "w");
		Check<Quaternion>(16, "x", "y", "z", "w");
		Check<Color3>(12, "r", "g", "b");
		Check<Color4>(16, "r", "g", "b", "a");
        CheckInterpolation();
        CheckEulerAngles();
        CheckVectors();
        Vector3 value = new(1.0f, 2.0f, 3.0f);
		Vector3 sum = value + Vector3.one;
		if (sum.x != 2.0f || sum.y != 3.0f || sum.z != 4.0f ||
			Quaternion.identity.w != 1.0f || default(Quaternion).w != 0.0f) {
			throw new InvalidOperationException("Math values changed.");
		}
	}

    private static void CheckVectors() {
        Vector3 value = new(3, 4, 0);
        if (value.magnitude != 5 || value.sqrMagnitude != 25 || value.normalized.sqrMagnitude < 0.99999f ||
            Vector3.Lerp(Vector3.zero, Vector3.one, 2) != Vector3.one ||
            Vector2.LerpUnclamped(Vector2.zero, Vector2.one, 2) != Vector2.one * 2) {
            throw new InvalidOperationException("Vector public contract failed.");
        }
        Vector3 near = value + new Vector3(0.000001f, 0, 0);
        if (value != near || value.Equals(near) || value.GetHashCode() != new Vector3(3, 4, 0).GetHashCode()) {
            throw new InvalidOperationException("Vector approximate and exact comparisons disagree.");
        }
        Vector3 rotated = Quaternion.Euler(0, 90, 0) * Vector3.forward;
        if ((rotated - Vector3.right).magnitude > 0.00001f ||
            MathF.Abs(Vector3.Slerp(Vector3.forward, Vector3.back, 0.5f).magnitude - 1) > 0.00001f) {
            throw new InvalidOperationException("Vector rotation or opposite interpolation failed.");
        }
        Vector3 velocity = Vector3.zero;
        Vector3 stopped = Vector3.SmoothDamp(Vector3.zero, Vector3.one, ref velocity, 0.5f, 0.0f, 0.1f);
        if (stopped != Vector3.zero || velocity != Vector3.zero) {
            throw new InvalidOperationException("SmoothDamp maxSpeed argument order changed.");
        }
    }

    // 複合回転と特異点を往復して向きを維持する
    private static void CheckEulerAngles() {
        Quaternion expected = Quaternion.AngleAxis(50, Vector3.up) * Quaternion.AngleAxis(30, Vector3.right) *
            Quaternion.AngleAxis(70, Vector3.forward);
        CheckSameRotation(expected, Quaternion.Euler(30, 50, 70));
        foreach (float x in new[] { -90f, -35f, 0f, 30f, 90f, 140f }) {
            foreach (float y in new[] { -160f, 25f, 90f }) {
                Quaternion original = Quaternion.Euler(x, y, 75f);
                CheckSameRotation(original, Quaternion.Euler(original.eulerAngles));
            }
        }
    }

    private static void CheckSameRotation(Quaternion a, Quaternion b) {
        if (MathF.Abs(Quaternion.Dot(a.normalized, b.normalized)) < 0.99999f) {
            throw new InvalidOperationException("ZXY Euler rotation changed.");
        }
    }

    // 補間の範囲と回転速度の違いを確認する
    private static void CheckInterpolation() {
        if (Mathf.Lerp(2, 4, 2) != 4 || Mathf.Lerp(2, 4, -1) != 2 || Mathf.LerpUnclamped(2, 4, 2) != 6) {
            throw new InvalidOperationException("Mathf interpolation range changed.");
        }
        Quaternion target = Quaternion.AngleAxis(120, Vector3.up);
        float linear = Quaternion.Angle(Quaternion.identity, Quaternion.Lerp(Quaternion.identity, target, 0.25f));
        float spherical = Quaternion.Angle(Quaternion.identity, Quaternion.Slerp(Quaternion.identity, target, 0.25f));
        if (MathF.Abs(spherical - 30) > 0.001f || MathF.Abs(linear - spherical) < 1 ||
            Quaternion.Angle(target, Quaternion.Slerp(Quaternion.identity, target, 2)) > 0.05f ||
            MathF.Abs(Quaternion.Angle(Quaternion.identity, Quaternion.SlerpUnclamped(Quaternion.identity, target, -0.5f)) - 60) > 0.001f) {
            throw new InvalidOperationException("Quaternion interpolation contract failed.");
        }
        Quaternion same = Quaternion.Slerp(target, -target, 0.5f);
        if (!float.IsFinite(same.w) || Quaternion.Angle(target, same) > 0.05f) {
            throw new InvalidOperationException("Equivalent quaternion interpolation failed.");
        }
    }

	// 型サイズとfieldの並びをNative側のfloat配置と照合する
	private static void Check<T>(int size, params string[] fields) where T : struct {

		if (Marshal.SizeOf<T>() != size) {
			throw new InvalidOperationException($"{typeof(T).Name} size changed.");
		}
		for (int i = 0; i < fields.Length; ++i) {
			if (Marshal.OffsetOf<T>(fields[i]).ToInt32() != i * sizeof(float)) {
				throw new InvalidOperationException($"{typeof(T).Name}.{fields[i]} offset changed.");
			}
		}
	}
}
