using System;

namespace NEMEngine;

// 自動生成されるLineRenderer拡張
public readonly partial struct LineRenderer {

    // 末尾へ1点追加する
    public void AddPoint(LinePoint point) {
        NativeApi.LineAddComponentPoint(entity.native, point);
    }

    // 末尾へ1点追加する
    public void AddPoint(Vector3 position, Color4 color, float thickness = 1.0f) {
        NativeApi.LineAddComponentPoint(entity.native, new LinePoint(position, color, thickness));
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
