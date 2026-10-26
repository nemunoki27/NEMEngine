#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleMeshSampling.h"
#include <Engine/Core/Rendering/Particle/ParticleGPUFrameInputs.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUState.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>

namespace Engine {

	class GraphicsCore;
	class RenderAssetLibrary;
	class PipelineStateCache;

	struct ParticleGPUSourceConstants {

		Matrix4x4 emitterWorld = Matrix4x4::Identity();
		Matrix4x4 inverseCaptureVP = Matrix4x4::Identity();
		Matrix4x4 previousWorld = Matrix4x4::Identity();
		uint32_t kind = 0;
		uint32_t candidateCount = 0;
		uint32_t vertexCount = 0;
		uint32_t indexOffset = 0;
		uint32_t vertexOffset = 0;
		uint32_t flags = 0;
		uint32_t stateSlot = UINT32_MAX;
		uint32_t moduleStateCount = 0;
		Vector4 textureSizeOffset{};
		Vector4 maskRange{};
		Vector4 imageSize{};
	};
	static_assert(sizeof(ParticleGPUSourceConstants) == 272);

	// 発生元の姿勢と借用資源をGPU更新中だけ固定する
	class ParticleGPUSourceResources {
	public:

		~ParticleGPUSourceResources();
		void Init(GraphicsCore& graphics);
		// 未読込の発生要求は捨てずに次frameへ保留する
		bool Prepare(GraphicsCore& graphics, ECSWorld& world, Entity owner, const ParticleGPUSimulationStep& step,
			RenderAssetLibrary& assets, PipelineStateCache& pipelines, const ParticleGPUFrameInputs& inputs, float deltaTime);
		// 更新Shaderへ同じframeの入力を接続する
		void Bind(GraphicsCore& graphics, const PipelineState& pipeline);
		// 次回の速度継承に使う頂点をGPU内で保存する
		void Finish(GraphicsCore& graphics);
		const std::string& GetDiagnostic() const { return diagnostic_; }
	private:

		struct FrameOwners {
			uint64_t serial = UINT64_MAX;
			std::vector<std::shared_ptr<const void>> values;
		};

		GraphicsResourceRetirement* retirement_ = nullptr;
		SRVDescriptor* descriptors_ = nullptr;
		StructuredInstanceBuffer<MeshVertex> emptyVertices_;
		StructuredInstanceBuffer<uint32_t> emptyIndices_;
		StructuredInstanceBuffer<float> emptyDistribution_;
		StructuredInstanceBuffer<Vector4> emptyColors_;
		StructuredInstanceBuffer<ParticleSourceSubMeshGPU> subMeshes_;
		std::unique_ptr<ParticleMeshSampling> sampling_;
		StructuredRWBuffer<MeshVertex> previousVertices_;
		std::vector<ParticleSourceSubMeshGPU> previousTransforms_;
		std::array<FrameOwners, kGraphicsFrameContextCount> owners_;
		std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 11> bindings_{};
		FrameConstantBufferAllocator constants_;
		ParticleGPUSourceConstants settings_{};
		Matrix4x4 previousEmitter_ = Matrix4x4::Identity();
		AssetID mesh_{};
		Entity renderer_{};
		uint64_t pose_ = UINT64_MAX;
		uint64_t buffer_ = UINT64_MAX;
		uint32_t generation_ = UINT32_MAX;
		uint32_t sourceGeneration_ = 0;
		uint32_t samplingCount_ = 0;
		ParticleSourceKind samplingKind_ = ParticleSourceKind::Analytic;
		ID3D12Resource* currentVertices_ = nullptr;
		uint64_t currentVertexOffset_ = 0;
		bool currentSkinned_ = false;
		bool previousReady_ = false;
		uint32_t emptyTexture_ = UINT32_MAX;
		uint32_t emptyDepth_ = UINT32_MAX;
		std::string diagnostic_;
		GraphicsFrameResourceRegistration frameRegistration_;

		// MeshのLOD0とサブメッシュ姿勢から分布を用意する
		bool PrepareMesh(GraphicsCore& graphics, ECSWorld& world, Entity owner, const ParticleGPUSimulationStep& step,
			RenderAssetLibrary& assets, PipelineStateCache& pipelines, const ParticleGPUFrameInputs& inputs);
		// 画像とCameraの同一世代の入力を接続する
		bool PrepareImage(GraphicsCore& graphics, ECSWorld& world, Entity owner, const ParticleSourceSettings& source,
			RenderAssetLibrary& assets, const ParticleGPUFrameInputs& inputs);
		// frameの完了まで借用先の再利用を防ぐ
		void Retain(std::shared_ptr<const void> owner);
	};
}
