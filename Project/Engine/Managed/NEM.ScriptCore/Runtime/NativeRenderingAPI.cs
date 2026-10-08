namespace NEMEngine;

using static NEMEngine.NativeAPI;

// 接続済みcallbackを用途別に呼び出す
internal static unsafe class NativeRenderingAPI {

    // RendererのMaterial値を設定する
    internal static bool WriteRendererMaterialParameter(NativeEntity entity, RendererMaterialTarget target,
        int subMeshIndex, ulong parameterID, string name, NativeMaterialParameterValue value) {

        if (SetRendererMaterialParameter == null ||
            parameterID == 0ul || string.IsNullOrEmpty(name)) {
            return false;
        }

        int capacity = ManagedUTF8Transfer.GetNullTerminatedCapacity(name);
        Span<byte> bytes = capacity <= 256
            ? stackalloc byte[capacity]
            : new byte[capacity];
        ManagedUTF8Transfer.WriteNullTerminated(name, bytes);
        fixed (byte* namePtr = bytes) {
            return SetRendererMaterialParameter(entity, (int)target, subMeshIndex, parameterID, namePtr, &value) != 0;
        }
    }

    // RendererのMaterial値を取得する
    internal static bool ReadRendererMaterialParameter(NativeEntity entity, RendererMaterialTarget target,
        int subMeshIndex, ulong parameterID, out NativeMaterialParameterValue value) {

        NativeMaterialParameterValue result = default;
        bool succeeded = GetRendererMaterialParameter != null &&
            parameterID != 0ul &&
            GetRendererMaterialParameter(entity, (int)target, subMeshIndex, parameterID, &result) != 0;
        value = result;
        return succeeded;
    }

    // RendererのMaterial値を解除する
    internal static bool ClearRendererMaterialParameterValue(NativeEntity entity, RendererMaterialTarget target,
        int subMeshIndex, ulong parameterID) {

        return ClearRendererMaterialParameter != null &&
            parameterID != 0ul &&
            ClearRendererMaterialParameter(entity, (int)target, subMeshIndex, parameterID) != 0;
    }

    // GPUのRay Tracing対応を返す
    internal static bool ReadRayTracingSupported() =>
        IsRayTracingSupported != null && IsRayTracingSupported() != 0;

    // 実行時のRay Tracing有効状態を返す
    internal static bool ReadRayTracingActive() =>
        IsRayTracingActive != null && IsRayTracingActive() != 0;

    // 名前から描画Passと世代を取得する
    internal static bool ResolveRenderFeaturePassValue(string passName, out ulong passID, out ulong generation) {

        passID = 0ul;
        generation = 0ul;
        if (ResolveRenderFeaturePass == null ||
            string.IsNullOrEmpty(passName)) {

            return false;
        }
        int capacity = ManagedUTF8Transfer.GetNullTerminatedCapacity(passName);
        Span<byte> bytes = capacity <= 256
            ? stackalloc byte[capacity]
            : new byte[capacity];
        ManagedUTF8Transfer.WriteNullTerminated(passName, bytes);
        fixed (byte* passNamePtr = bytes)
        fixed (ulong* passIDPtr = &passID)
        fixed (ulong* generationPtr = &generation) {
            return ResolveRenderFeaturePass(passNamePtr, passIDPtr, generationPtr) != 0;
        }
    }

    // Lineの点列を置き換える
    internal static void LineSetComponentPoints(NativeEntity entity, ReadOnlySpan<LinePoint> points, bool loop) {

        if (LineSetPoints == null) {
            return;
        }
        fixed (LinePoint* p = points) {
            LineSetPoints(entity, p, points.Length, loop ? 1 : 0);
        }
    }

    // Lineへ点を追加する
    internal static int LineAddComponentPoint(NativeEntity entity, LinePoint point) {

        if (LineAddPoint == null) {
            return -1;
        }
        return LineAddPoint(entity, point);
    }

    // Lineの指定点を更新する
    internal static void LineUpdateComponentPoint(NativeEntity entity, LinePoint point) {

        if (LineUpdatePoint == null) {
            return;
        }
        LineUpdatePoint(entity, point);
    }

    // 点列を即時描画する
    internal static void LineDrawImmediatePolyline(ReadOnlySpan<LinePoint> points, bool loop, bool is2D,
        AssetGUID materialID) {

        if (LineDrawImmediate == null || points.Length < 2) {
            return;
        }
        fixed (LinePoint* p = points) {
            LineDrawImmediate(p, points.Length, loop ? 1 : 0, is2D ? 1 : 0, materialID);
        }
    }

    // 球を即時描画する
    internal static void LineDrawImmediateSphere(Vector3 center, float radius, Color4 color, int division,
        float thickness, AssetGUID materialID) {

        if (LineDrawSphereImmediate == null) {
            return;
        }
        LineDrawSphereImmediate(NativeVector3.From(center), radius, NativeColor4.From(color), division, thickness,
            materialID);
    }

    // 指定形状を即時描画する
    internal static void LineDrawShapeImmediate(NativeLineShape shape) {

        if (LineDrawShape == null) {
            return;
        }
        LineDrawShape(&shape);
    }

    // Particleの再生操作を予約する
    internal static void ParticleSystemControlCall(NativeEntity entity, int operation,
        ParticleSystemStopBehavior stopBehavior, bool withChildren) {

        if (ParticleSystemControl != null) {
            ParticleSystemControl(entity, operation, (int)stopBehavior, withChildren ? 1 : 0);
        }
    }

    // Particleの実行状態を返す
    internal static int ParticleSystemStateCall(NativeEntity entity, int state, bool withChildren = false) =>
        ParticleSystemState != null ?
            ParticleSystemState(entity, state, withChildren ? 1 : 0) : 0;

    // 描画PassのIDと世代を検証する
    internal static bool ValidateRenderFeaturePassValue(ulong passID, ulong generation) =>
        ValidateRenderFeaturePass != null && passID != 0ul &&
        generation != 0ul &&
        ValidateRenderFeaturePass(passID, generation) != 0;

    // 描画Passの実行状態を設定する
    internal static bool WriteRenderFeaturePassEnabled(ulong passID, ulong generation, bool enabled) =>
        SetRenderFeaturePassEnabled != null && passID != 0ul &&
        generation != 0ul && SetRenderFeaturePassEnabled(
            passID, generation, enabled ? 1 : 0) != 0;

    // 描画Passの画面出力を設定する
    internal static bool WriteRenderFeaturePassSceneColorOutput(ulong passID, ulong generation, bool enabled) =>
        SetRenderFeaturePassSceneColorOutput != null &&
        SetRenderFeaturePassSceneColorOutput(passID, generation, enabled ? 1 : 0) != 0;

    // 描画PassのParameterを設定する
    internal static bool WriteRenderFeaturePassParameter(ulong passID, ulong generation, ulong parameterID,
        string parameterName, NativeMaterialParameterValue value) {

        if (SetRenderFeaturePassParameter == null || parameterID == 0ul ||
            passID == 0ul || generation == 0ul ||
            string.IsNullOrEmpty(parameterName)) {

            return false;
        }
        int parameterCapacity = ManagedUTF8Transfer.GetNullTerminatedCapacity(parameterName);
        Span<byte> parameterBytes = parameterCapacity <= 256
            ? stackalloc byte[parameterCapacity]
            : new byte[parameterCapacity];
        ManagedUTF8Transfer.WriteNullTerminated(parameterName, parameterBytes);
        fixed (byte* parameterNamePtr = parameterBytes) {
            return SetRenderFeaturePassParameter(passID, generation, parameterID, parameterNamePtr, &value) != 0;
        }
    }

    // 描画PassのParameterを取得する
    internal static bool ReadRenderFeaturePassParameter(ulong passID, ulong generation, ulong parameterID,
        out NativeMaterialParameterValue value) {

        NativeMaterialParameterValue result = default;
        bool found = GetRenderFeaturePassParameter != null &&
            passID != 0ul && generation != 0ul && parameterID != 0ul &&
            GetRenderFeaturePassParameter(passID, generation, parameterID, &result) != 0;
        value = result;
        return found;
    }

    // 描画Groupの実行状態を設定する
    internal static bool WriteRenderFeatureGroupEnabled(string groupName, bool enabled) {

        if (SetRenderFeatureGroupEnabled == null ||
            string.IsNullOrEmpty(groupName)) {
            return false;
        }
        int capacity = ManagedUTF8Transfer.GetNullTerminatedCapacity(groupName);
        Span<byte> bytes = capacity <= 256
            ? stackalloc byte[capacity]
            : new byte[capacity];
        ManagedUTF8Transfer.WriteNullTerminated(groupName, bytes);
        fixed (byte* groupNamePtr = bytes) {
            return SetRenderFeatureGroupEnabled(groupNamePtr, enabled ? 1 : 0) != 0;
        }
    }

    // 描画PassのParameterを解除する
    internal static bool ClearRenderFeaturePassParameterValue(ulong passID, ulong generation, ulong parameterID) =>
        ClearRenderFeaturePassParameter != null && passID != 0ul &&
        generation != 0ul && parameterID != 0ul &&
        ClearRenderFeaturePassParameter(passID, generation, parameterID) != 0;

    // 描画Passの実行変更を解除する
    internal static bool ResetRenderFeaturePassValue(ulong passID, ulong generation) =>
        ResetRenderFeaturePass != null && passID != 0ul &&
        generation != 0ul &&
        ResetRenderFeaturePass(passID, generation) != 0;

    // 描画構成の実行変更を解除する
    internal static void ResetAllRenderFeatureOverrides() {

        if (ResetRenderFeatureOverrides != null) {
            ResetRenderFeatureOverrides();
        }
    }

}
