using System;

namespace NEMEngine;

// 自動生成されるLineRenderer拡張
public sealed partial class LineRenderer {

    public DynamicBuffer<LineRendererPoint> Points =>
        entity.GetBuffer<LineRendererPoint>();

    public MaterialInstance MaterialInstance =>
        new(entity, RendererMaterialTarget.Line);

    public void SetColor(Color4 color) {
        MaterialInstance.SetColor(MaterialParameterIDs.BaseColor, MaterialParameterNames.BaseColor, color);
    }

    public Color4 GetColor() {
        return MaterialInstance.TryGetColor(MaterialParameterNames.BaseColor, out Color4 color)
            ? color
            : new Color4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    // 末尾へ1点追加し、indexを採番したLinePointを返す。後でUpdatePointに渡して更新できる
    public LinePoint AddPoint(LinePoint point) {
        point.index = NativeRenderingAPI.LineAddComponentPoint(entity.native, point);
        return point;
    }

    // 末尾へ1点追加し、indexを採番したLinePointを返す
    public LinePoint AddPoint(Vector3 position, Color4 color, float thickness = 1.0f) {
        return AddPoint(new LinePoint(position, color, thickness));
    }

    // AddPointで得たLinePointのindexの点を、座標・色・太さごと更新する
    public void UpdatePoint(LinePoint point) {
        NativeRenderingAPI.LineUpdateComponentPoint(entity.native, point);
    }

    // 点列を差し替える。loopで始点と終点をつなぐ
    public void SetPoints(ReadOnlySpan<LinePoint> points, bool loop = false) {
        NativeRenderingAPI.LineSetComponentPoints(entity.native, points, loop);
    }

    // 点列をクリアする
    public void Clear() {
        NativeRenderingAPI.LineSetComponentPoints(entity.native, ReadOnlySpan<LinePoint>.Empty, false);
    }
}
