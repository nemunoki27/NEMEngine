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
		Vector3 value = new(1.0f, 2.0f, 3.0f);
		Vector3 sum = value + Vector3.one;
		if (sum.x != 2.0f || sum.y != 3.0f || sum.z != 4.0f ||
			Quaternion.identity.w != 1.0f || default(Quaternion).w != 0.0f) {
			throw new InvalidOperationException("Math values changed.");
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
