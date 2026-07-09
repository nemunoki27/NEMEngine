#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/BuiltinRenderBackendBase.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleBatchResources.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveGeometryManager.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshGPUResourceManager.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/FrameBatchResourcePool.h>

namespace Engine {

	// front
	struct ParticleRenderSettings;
	class IParticleParametricShape;

	//============================================================================
	//	ParticleRenderBackend class
	//	粒子を共有ジオメトリでインスタンシング描画する、形状アニメはMS対応GPUのみパラメトリック生成する
	//============================================================================
	class ParticleRenderBackend :
		public BuiltinRenderBackendBase {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleRenderBackend() {

			shapeConstantsCBVSlot_ = perDrawBindCache_.AddSlot("ParticleShapeConstants", ShaderBindingKind::CBV);
			verticesSRVSlot_ = perDrawBindCache_.AddSlot("gVertices", ShaderBindingKind::SRV);
			instancesSRVSlot_ = perDrawBindCache_.AddSlot("gInstances", ShaderBindingKind::SRV);
			trailVerticesSRVSlot_ = perDrawBindCache_.AddSlot("gTrailVertices", ShaderBindingKind::SRV);
		}
		~ParticleRenderBackend() override;

		void BeginFrame(GraphicsCore& graphicsCore) override;

		void DrawBatch(const RenderDrawContext& context, std::span<const RenderItem* const> items) override;

		//--------- accessor -----------------------------------------------------

		uint32_t GetID() const override { return RenderBackendID::Particle; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 粒子が使う共有ジオメトリ
		PrimitiveGeometryManager geometryManager_{};
		bool geometryManagerInitialized_ = false;

		// Model粒子が使うメッシュのGPUリソース
		MeshGPUResourceManager meshResourceManager_{};
		bool meshManagerInitialized_ = false;

		FrameBatchResourcePool<ParticleBatchResources> resourcePool_;

		PipelineBindingCache::SlotID shapeConstantsCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID verticesSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID instancesSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID trailVerticesSRVSlot_ = PipelineBindingCache::kInvalidSlot;

		//--------- functions ----------------------------------------------------

		// バッチのインスタンスデータをフェーズごとに集める、粒子ごとにビルボードのワールド行列を作る
		void CollectInstances(const RenderDrawContext& context, std::span<const RenderItem* const> items,
			std::vector<ParticleInstanceData>& outInstances, std::vector<uint32_t>& outPhaseCounts) const;
		// パラメトリックMS生成で描画する、パイプラインを解決できなければfalse
		bool DrawParametricShapePath(const RenderDrawContext& context, const RenderItem* item,
			const IParticleParametricShape& parametric, const ParticleRenderSettings& settings,
			const BackendDrawCommon::ResolvedMaterialPass& resolvedPass,
			D3D12_GPU_VIRTUAL_ADDRESS instancesAddress, uint32_t instanceCount,
			D3D12_GPU_VIRTUAL_ADDRESS viewAddress);
		// Model粒子をインスタンシング描画する、メッシュを解決できなければfalse
		bool DrawModelMeshPath(const RenderDrawContext& context, const RenderItem* item,
			const ParticleRenderSettings& settings,
			const BackendDrawCommon::ResolvedMaterialPass& resolvedPass,
			D3D12_GPU_VIRTUAL_ADDRESS instancesAddress, uint32_t instanceCount,
			D3D12_GPU_VIRTUAL_ADDRESS viewAddress);
		// 共有ジオメトリでインスタンシング描画する
		void DrawSharedGeometryPath(const RenderDrawContext& context, const RenderItem* item,
			const ParticleRenderSettings& settings,
			const BackendDrawCommon::ResolvedMaterialPass& resolvedPass,
			D3D12_GPU_VIRTUAL_ADDRESS instancesAddress, uint32_t instanceCount,
			D3D12_GPU_VIRTUAL_ADDRESS viewAddress);
		// トレイルのリボン頂点を構築する
		void BuildTrailVertices(const RenderDrawContext& context, std::span<const RenderItem* const> items,
			std::vector<ParticleTrailVertex>& outVertices) const;
		// トレイルを描画する
		void DrawTrails(const RenderDrawContext& context, const RenderItem* item,
			const BackendDrawCommon::ResolvedMaterialPass& resolvedPass, ParticleBatchResources& resources);
	};
} // Engine
