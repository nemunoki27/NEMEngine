#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

// c++
#include <vector>
#include <cstdint>

namespace Engine {

	// front
	class GraphicsCore;

	//============================================================================
	//	PrimitiveInstanceData
	//	インスタンスごとのワールド行列とUV行列とライティングフラグ、StructuredBufferで読む
	//============================================================================
	struct PrimitiveInstanceData {

		Matrix4x4 worldMatrix = Matrix4x4::Identity();
		Matrix4x4 uvMatrix = Matrix4x4::Identity();
		uint32_t flags = 0;
		uint32_t pad0 = 0;
		uint32_t pad1 = 0;
		uint32_t pad2 = 0;
	};

	//============================================================================
	//	PrimitiveBatchResources class
	//	バッチ1つ分のインスタンスバッファを管理する
	//============================================================================
	class PrimitiveBatchResources {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PrimitiveBatchResources() = default;
		~PrimitiveBatchResources() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// インスタンス配列をアップロードする
		void UploadInstances(const std::vector<PrimitiveInstanceData>& instances);

		//--------- accessor -----------------------------------------------------

		D3D12_GPU_VIRTUAL_ADDRESS GetInstancesGPUAddress() const { return instances_.GetGPUAddress(); }
		uint32_t GetInstanceCount() const { return instanceCount_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		StructuredInstanceBuffer<PrimitiveInstanceData> instances_{ "gInstances" };
		uint32_t instanceCount_ = 0;
		bool initialized_ = false;
	};
} // Engine
