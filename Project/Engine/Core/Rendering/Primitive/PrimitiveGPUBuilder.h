#pragma once

namespace Engine {

	class GraphicsCore;
	class SRVDescriptor;
	struct PrimitiveMeshData;
	struct PrimitiveGeometry;
	namespace PrimitiveGPUBuilder {
		// CPU形状から描画とBLAS用のGPU資源を生成する
		bool Create(GraphicsCore& graphicsCore, SRVDescriptor* srvDescriptor,
			const PrimitiveMeshData& mesh, PrimitiveGeometry& geometry);
	}
}
