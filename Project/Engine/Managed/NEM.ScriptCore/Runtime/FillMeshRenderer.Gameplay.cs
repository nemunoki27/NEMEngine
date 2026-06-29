using System;

namespace NEMEngine;

// 自動生成されるFillMeshRenderer拡張
public readonly partial struct FillMeshRenderer {

    // 面を構成する点列を差し替える、XZ平面でY座標は構築時に0へ固定される
    public void SetFacePositions(ReadOnlySpan<Vector3> positions) {
        NativeApi.FillMeshSetFacePositions(entity.native, positions);
    }

    // 点列をクリアする
    public void Clear() {
        NativeApi.FillMeshSetFacePositions(entity.native, ReadOnlySpan<Vector3>.Empty);
    }
}
