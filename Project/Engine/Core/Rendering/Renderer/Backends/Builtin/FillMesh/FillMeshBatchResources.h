#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Vector4.h>

// c++
#include <vector>
#include <cstdint>

namespace Engine {

	// front
	class GraphicsCore;

	//============================================================================
	//	FillMeshVertex
	//	StructuredBufferで読むため16byte境界に揃える
	//============================================================================
	struct FillMeshVertex {

		Vector4 position = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
		Vector4 normal = Vector4(0.0f, 1.0f, 0.0f, 0.0f);
	};

	//============================================================================
	//	FillMeshBatchResources class
	//	面メッシュ1体分の頂点バッファを管理する
	//============================================================================
	class FillMeshBatchResources {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		FillMeshBatchResources() = default;
		~FillMeshBatchResources() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// 三角形分割インデックスから展開頂点を作りアップロードする、Yは0固定
		void UploadVertices(const std::vector<Vector3>& positions, const std::vector<uint32_t>& indices);

		//--------- accessor -----------------------------------------------------

		D3D12_GPU_VIRTUAL_ADDRESS GetVerticesGPUAddress() const { return vertices_.GetGPUAddress(); }
		uint32_t GetVertexCount() const { return vertexCount_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		StructuredInstanceBuffer<FillMeshVertex> vertices_{ "gVertices" };
		std::vector<FillMeshVertex> scratch_{};
		uint32_t vertexCount_ = 0;
		bool initialized_ = false;
	};
} // Engine
