#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleGPUWorldInputs.h"
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>

namespace Engine {

	class PipelineState;

	struct ParticleGPUDepthInput {

		D3D12_GPU_DESCRIPTOR_HANDLE srv{};
		Matrix4x4 viewProjection = Matrix4x4::Identity();
		Matrix4x4 inverseViewProjection = Matrix4x4::Identity();
		uint32_t width = 0;
		uint32_t height = 0;
		// DescriptorとResourceを所有する入力側の寿命Token
		std::shared_ptr<const void> owner;
	};

	// World入力だけを独立したframe別Upload領域へ保持する
	class ParticleGPUWorldResources {
	public:

		~ParticleGPUWorldResources();
		void Init(ID3D12Device* device, SRVDescriptor& descriptors, GraphicsResourceRetirement& retirement);
		// 同じframeの先行Stepへ影響しない領域へ転送する
		void Upload(std::shared_ptr<const ParticleGPUWorldSnapshot> snapshot);
		// 深度Prepassの完了後に可読SRVとCamera行列を接続する
		void Bind(ID3D12GraphicsCommandList* commands, const PipelineState& pipeline, const ParticleGPUDepthInput& depth = {});
	private:

		struct FrameOwners {

			uint64_t serial = UINT64_MAX;
			std::vector<std::shared_ptr<const void>> values;
		};

		ID3D12Device* device_ = nullptr;
		SRVDescriptor* descriptors_ = nullptr;
		GraphicsResourceRetirement* retirement_ = nullptr;
		uint32_t emptyDepth_ = UINT32_MAX;
		std::shared_ptr<const ParticleGPUWorldSnapshot> snapshot_;
		std::array<FrameOwners, kGraphicsFrameContextCount> owners_;
		FrameConstantBufferAllocator constants_;
		StructuredInstanceBuffer<ParticleGPUForceField> forces_;
		StructuredInstanceBuffer<ParticleGPUVectorField> vectorFields_;
		StructuredInstanceBuffer<Vector4> vectorSamples_;
		StructuredInstanceBuffer<ParticleGPUSplineSample> splineSamples_;
		StructuredInstanceBuffer<ParticleGPUSplineRange> splineRanges_;
		StructuredInstanceBuffer<ParticleGPUCollider> colliders_;
		StructuredInstanceBuffer<ParticleGPUCollisionNode> collisionNodes_;
		StructuredInstanceBuffer<ParticleGPUHeightmap> heightmaps_;
		StructuredInstanceBuffer<float> heightSamples_;
		GraphicsFrameResourceRegistration frameRegistration_;

		// frame再利用まで参照元のDescriptorと入力を保持する
		void Retain(std::shared_ptr<const void> owner);
	};
}
