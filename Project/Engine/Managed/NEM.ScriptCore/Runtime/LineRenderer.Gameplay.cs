using System;

namespace NEMEngine;

// 自動生成されるLineRenderer拡張
public readonly partial struct LineRenderer {

    // 末尾へ1点追加し、indexを採番したLinePointを返す。後でUpdatePointに渡して更新できる
    public LinePoint AddPoint(LinePoint point) {
        point.index = NativeApi.LineAddComponentPoint(entity.native, point);
        return point;
    }

    // 末尾へ1点追加し、indexを採番したLinePointを返す
    public LinePoint AddPoint(Vector3 position, Color4 color, float thickness = 1.0f) {
        return AddPoint(new LinePoint(position, color, thickness));
    }

    // AddPointで得たLinePointのindexの点を、座標・色・太さごと更新する
    public void UpdatePoint(LinePoint point) {
        NativeApi.LineUpdateComponentPoint(entity.native, point);
    }

    // 点列を差し替える。loopで始点と終点をつなぐ
    public void SetPoints(ReadOnlySpan<LinePoint> points, bool loop = false) {
        NativeApi.LineSetComponentPoints(entity.native, points, loop);
    }

    // 点列をクリアする
    public void Clear() {
        NativeApi.LineSetComponentPoints(entity.native, ReadOnlySpan<LinePoint>.Empty, false);
    }
}
