using System.Text;

namespace NEMEngine;

using static NEMEngine.NativeAPI;

// 接続済みcallbackを用途別に呼び出す
internal static unsafe class NativeRenderingAPI {

    internal static bool WriteRendererMaterialParameter(
        NativeEntity entity, RendererMaterialTarget target, int subMeshIndex,
        ulong parameterID, string name, NativeMaterialParameterValue value) {

        if (SetRendererMaterialParameter == null ||
            parameterID == 0ul || string.IsNullOrEmpty(name)) {
            return false;
        }

        int byteCount = Encoding.UTF8.GetByteCount(name);
        Span<byte> bytes = byteCount < 256
            ? stackalloc byte[byteCount + 1]
            : new byte[byteCount + 1];
        Encoding.UTF8.GetBytes(name, bytes);
        bytes[byteCount] = 0;
        fixed (byte* namePtr = bytes) {
            return SetRendererMaterialParameter(
                entity, (int)target, subMeshIndex, parameterID,
                namePtr, &value) != 0;
        }
    }

    internal static bool ReadRendererMaterialParameter(
        NativeEntity entity, RendererMaterialTarget target, int subMeshIndex,
        ulong parameterID, out NativeMaterialParameterValue value) {

        NativeMaterialParameterValue result = default;
        bool succeeded = GetRendererMaterialParameter != null &&
            parameterID != 0ul &&
            GetRendererMaterialParameter(
                entity, (int)target, subMeshIndex, parameterID, &result) != 0;
        value = result;
        return succeeded;
    }

    internal static bool ClearRendererMaterialParameterValue(
        NativeEntity entity, RendererMaterialTarget target,
        int subMeshIndex, ulong parameterID) {

        return ClearRendererMaterialParameter != null &&
            parameterID != 0ul &&
            ClearRendererMaterialParameter(
                entity, (int)target, subMeshIndex, parameterID) != 0;
    }

    internal static bool ReadRayTracingSupported() =>
        IsRayTracingSupported != null && IsRayTracingSupported() != 0;

    internal static bool ReadRayTracingActive() =>
        IsRayTracingActive != null && IsRayTracingActive() != 0;

    internal static bool ResolveRenderFeaturePassValue(
		string passName, out ulong passID, out ulong generation) {

		passID = 0ul;
		generation = 0ul;
		if (ResolveRenderFeaturePass == null ||
			string.IsNullOrEmpty(passName)) {

			return false;
		}
		int byteCount = Encoding.UTF8.GetByteCount(passName);
		Span<byte> bytes = byteCount < 256
			? stackalloc byte[byteCount + 1]
			: new byte[byteCount + 1];
		Encoding.UTF8.GetBytes(passName, bytes);
		bytes[byteCount] = 0;
		fixed (byte* passNamePtr = bytes)
		fixed (ulong* passIDPtr = &passID)
		fixed (ulong* generationPtr = &generation) {
			return ResolveRenderFeaturePass(
				passNamePtr, passIDPtr, generationPtr) != 0;
		}
	}

    internal static void LineSetComponentPoints(NativeEntity entity, ReadOnlySpan<LinePoint> points, bool loop) {
        if (LineSetPoints == null) {
            return;
        }
        fixed (LinePoint* p = points) {
            LineSetPoints(entity, p, points.Length, loop ? 1 : 0);
        }
    }

    internal static int LineAddComponentPoint(NativeEntity entity, LinePoint point) {
        if (LineAddPoint == null) {
            return -1;
        }
        return LineAddPoint(entity, point);
    }

    internal static void LineUpdateComponentPoint(NativeEntity entity, LinePoint point) {
        if (LineUpdatePoint == null) {
            return;
        }
        LineUpdatePoint(entity, point);
    }

    internal static void LineDrawImmediatePolyline(ReadOnlySpan<LinePoint> points, bool loop, bool is2D, AssetGUID materialID) {
        if (LineDrawImmediate == null || points.Length < 2) {
            return;
        }
        fixed (LinePoint* p = points) {
            LineDrawImmediate(p, points.Length, loop ? 1 : 0, is2D ? 1 : 0, materialID);
        }
    }

    internal static void LineDrawImmediateSphere(Vector3 center, float radius, Color4 color, int division, float thickness, AssetGUID materialID) {
        if (LineDrawSphereImmediate == null) {
            return;
        }
        LineDrawSphereImmediate(NativeVector3.From(center), radius, NativeColor4.From(color), division, thickness, materialID);
    }

    internal static void LineDrawShapeImmediate(NativeLineShape shape) {
        if (LineDrawShape == null) {
            return;
        }
        LineDrawShape(&shape);
    }

    internal static void ParticleSystemControlCall(
        NativeEntity entity, int operation,
        ParticleSystemStopBehavior stopBehavior, bool withChildren) {

        if (ParticleSystemControl != null) {
            ParticleSystemControl(entity, operation,
                (int)stopBehavior, withChildren ? 1 : 0);
        }
    }

    internal static int ParticleSystemStateCall(
        NativeEntity entity, int state, bool withChildren = false) =>
        ParticleSystemState != null ?
            ParticleSystemState(entity, state, withChildren ? 1 : 0) : 0;
}
