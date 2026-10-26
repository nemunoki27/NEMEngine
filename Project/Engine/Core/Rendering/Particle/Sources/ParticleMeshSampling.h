#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/GPUInclusiveScan.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleSourceSettings.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

namespace Engine {

	struct ParticleSourceSubMeshGPU {

		Matrix4x4 world = Matrix4x4::Identity();
		Matrix4x4 normal = Matrix4x4::Identity();
		Matrix4x4 previousWorld = Matrix4x4::Identity();
		uint32_t enabled = 1;
		uint32_t reserved[3]{};
	};
	static_assert(sizeof(ParticleSourceSubMeshGPU) == 208);

	struct ParticleMeshSamplingInput {

		D3D12_GPU_VIRTUAL_ADDRESS vertices = 0;
		D3D12_GPU_VIRTUAL_ADDRESS indices = 0;
		D3D12_GPU_VIRTUAL_ADDRESS vertexSubMeshes = 0;
		D3D12_GPU_VIRTUAL_ADDRESS subMeshes = 0;
		uint32_t vertexCount = 0;
		uint32_t indexOffset = 0;
		uint32_t indexCount = 0;
		uint32_t subMeshCount = 0;
		ParticleSourceKind kind = ParticleSourceKind::MeshSurface;
	};

	struct ParticleMeshSamplingConstants {

		uint32_t vertexCount = 0;
		uint32_t indexOffset = 0;
		uint32_t indexCount = 0;
		uint32_t kind = 0;
		uint32_t operation = 0;
		uint32_t subMeshCount = 0;
		uint32_t elementCount = 0;
		uint32_t groupCountX = 1;
	};
	static_assert(sizeof(ParticleMeshSamplingConstants) == 32);

	//============================================================================
	//	ParticleMeshSampling class
	//	頂点数と辺長と面積をGPUで累積し発生位置の分布を作る
	//============================================================================
	class ParticleMeshSampling {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 発生元を使うときだけ累積和と頂点印を確保する
		void Init(ID3D12Device* device, SRVDescriptor& descriptors, const ParticleMeshSamplingInput& input);
		// Meshと姿勢の世代が変わったときに分布を更新する
		void Record(ID3D12Device* device, DxCommand& commands, GraphicsResourceRetirement& retirement,
			const PipelineState& prepare, const PipelineState& scan, const ParticleMeshSamplingInput& input,
			FrameConstantBufferAllocator& constants);

		//--------- accessor -----------------------------------------------------

		uint32_t GetCount() const { return count_; }
		StructuredRWBuffer<float>& GetDistribution() { return distribution_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		StructuredRWBuffer<float> distribution_;
		StructuredRWBuffer<uint32_t> usedVertices_;
		GPUInclusiveScanPlan plan_{};
		uint32_t count_ = 0;
		uint32_t vertexCount_ = 0;
		uint32_t indexCount_ = 0;
		ParticleSourceKind kind_ = ParticleSourceKind::Analytic;
	};
}
