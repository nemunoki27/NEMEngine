#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector4.h>
#include <Engine/Core/Foundation/Math/Color.h>

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
		Matrix4x4 previousWorldMatrix = Matrix4x4::Identity();
		Matrix4x4 uvMatrix = Matrix4x4::Identity();
		// Cylinderの上面、中心、下面半径と高さ
		Vector4 shapeParams0 = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		// Cylinderの上面Weight、下面Weight、未使用値、形状フラグ
		Vector4 shapeParams1 = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		Color4 topColor = Color4::White();
		Color4 centerColor = Color4::White();
		Color4 bottomColor = Color4::White();
		uint32_t flags = 0;
		uint32_t motionFrameSerial = 0;
		uint32_t entityIndex = UINT32_MAX;
		uint32_t entityGeneration = UINT32_MAX;
	};
	static_assert(sizeof(PrimitiveInstanceData) == 288);

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
