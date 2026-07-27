using System;
using System.Collections.Generic;

namespace NEMEngine;

// 自動生成されるFillMeshRenderer拡張
public sealed partial class FillMeshRenderer {

    public DynamicBuffer<FillMeshPoint> Points =>
        entity.GetBuffer<FillMeshPoint>();

    // 面を構成する点列を差し替える、XZ平面でY座標は構築時に0へ固定される
    public void SetFacePositions(ReadOnlySpan<Vector3> positions) {
        NativeApi.FillMeshSetFacePositions(entity.native, positions);
    }

    // 点列をクリアする
    public void Clear() {
        NativeApi.FillMeshSetFacePositions(entity.native, ReadOnlySpan<Vector3>.Empty);
    }

    // ローカル座標の点列を取得する
    public List<Vector3> GetLocalPoints() {
        return NativeApi.FillMeshGetPoints(entity.native, false);
    }

    // ワールド座標の点列を取得する
    public List<Vector3> GetWorldPoint() {
        return NativeApi.FillMeshGetPoints(entity.native, true);
    }
}
