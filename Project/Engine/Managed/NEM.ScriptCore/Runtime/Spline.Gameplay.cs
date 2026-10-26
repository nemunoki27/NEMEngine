using System.Runtime.InteropServices;

namespace NEMEngine;

[StructLayout(LayoutKind.Sequential)]
public struct SplineControlPoint {

	public Vector3 position;
	public Vector3 tangentIn;
	public Vector3 tangentOut;

	public SplineControlPoint(Vector3 position, Vector3 tangentIn, Vector3 tangentOut) {
		this.position = position;
		this.tangentIn = tangentIn;
		this.tangentOut = tangentOut;
	}
}

[StructLayout(LayoutKind.Sequential)]
public struct SplineSample {

	public Vector3 position;
	public float distance;
	public Vector3 tangent;
}

// 制御点はローカル座標、距離評価はワールド座標を返す
public sealed partial class Spline {

	public int pointCount {
		get => Read<int>(0);
		set {
			if (value < 0 || value > 4096) throw new ArgumentOutOfRangeException(nameof(value));
			Write(0, value);
		}
	}

	public bool closed { get => Read<int>(1) != 0; set => Write(1, value ? 1 : 0); }
	public float tolerance { get => Read<float>(2); set => Write(2, value); }

	public SplineControlPoint GetPoint(int index) {
		if ((uint)index >= (uint)pointCount) throw new ArgumentOutOfRangeException(nameof(index));
		return Read<SplineControlPoint>(3, index);
	}

	public void SetPoint(int index, SplineControlPoint point) {
		if ((uint)index >= (uint)pointCount) throw new ArgumentOutOfRangeException(nameof(index));
		Write(3, point, index);
	}

	public unsafe SplineSample EvaluateDistance(float distance, bool loop = true) {
		if (NativeAPI.SplineEvaluate == null) throw new NotSupportedException("Spline evaluation is not connected.");
		SplineSample sample;
		if (NativeAPI.SplineEvaluate(native, distance, loop ? 1 : 0, &sample, sizeof(SplineSample)) == 0)
			throw new InvalidOperationException("Spline cannot be evaluated.");
		return sample;
	}

	private unsafe T Read<T>(int property, int index = 0) where T : unmanaged {
		if (NativeAPI.SplineGetProperty == null) throw new NotSupportedException("Spline getter is not connected.");
		T value;
		if (NativeAPI.SplineGetProperty(native, property, index, &value, sizeof(T)) == 0)
			throw new InvalidOperationException("Spline property cannot be read.");
		return value;
	}

	private unsafe void Write<T>(int property, T value, int index = 0) where T : unmanaged {
		if (NativeAPI.SplineSetProperty == null) throw new NotSupportedException("Spline setter is not connected.");
		if (NativeAPI.SplineSetProperty(native, property, index, &value, sizeof(T)) == 0)
			throw new InvalidOperationException("Spline property cannot be changed.");
	}
}
