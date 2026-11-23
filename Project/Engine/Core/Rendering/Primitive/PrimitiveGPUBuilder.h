#pragma once

#include <Engine/Core/Foundation/Math/Vector4.h>
#include <vector>

namespace Engine {

	class GraphicsCore;
	class SRVDescriptor;
	struct PrimitiveMeshData;
	struct PrimitiveGeometry;
	struct MeshVertex;
	namespace PrimitiveGPUBuilder {
		void CreateIndices(GraphicsCore& graphicsCore, SRVDescriptor& descriptor, const PrimitiveMeshData& mesh, PrimitiveGeometry& geometry);
		void ConvertVertices(const PrimitiveMeshData& mesh, std::vector<MeshVertex>& vertices, Vector4& planarUVBounds);
		// CPU形状から描画とBLAS用のGPU資源を生成する
		bool Create(GraphicsCore& graphicsCore, SRVDescriptor* srvDescriptor,
			const PrimitiveMeshData& mesh, PrimitiveGeometry& geometry);
	}
}
