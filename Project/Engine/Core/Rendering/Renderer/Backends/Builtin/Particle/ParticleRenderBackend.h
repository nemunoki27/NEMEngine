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

// c++
#include <string>
#include <unordered_map>

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
			trailConstantsCBVSlot_ = perDrawBindCache_.AddSlot("ParticleTrailConstants", ShaderBindingKind::CBV);
			verticesSRVSlot_ = perDrawBindCache_.AddSlot("gVertices", ShaderBindingKind::SRV);
			geometrySRVSlot_ = perDrawBindCache_.AddSlot("gParticleGeometry", ShaderBindingKind::SRV);
			materialsSRVSlot_ = perDrawBindCache_.AddSlot("gParticleMaterials", ShaderBindingKind::SRV);
			customParametersSRVSlot_ = perDrawBindCache_.AddSlot("gParticleCustomParameters", ShaderBindingKind::SRV);
			trailPointsSRVSlot_ = perDrawBindCache_.AddSlot("gTrailPoints", ShaderBindingKind::SRV);
			trailSegmentsSRVSlot_ = perDrawBindCache_.AddSlot("gTrailSegments", ShaderBindingKind::SRV);
		}
		~ParticleRenderBackend() override;

		// Model形状で使う全メッシュを同期作成する
		void PreloadMeshes(GraphicsCore& graphicsCore, AssetDatabase& assetDatabase,
			std::span<const AssetID> meshAssets);

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
		ParticleTrailRenderData trailDataScratch_{};

		PipelineBindingCache::SlotID shapeConstantsCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID trailConstantsCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID verticesSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID geometrySRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID materialsSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID customParametersSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID trailPointsSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID trailSegmentsSRVSlot_ = PipelineBindingCache::kInvalidSlot;

		//--------- functions ----------------------------------------------------

		// バッチのインスタンスデータをフェーズごとに集める、粒子ごとにビルボードのワールド行列を作る
		void CollectInstances(const RenderDrawContext& context, std::span<const RenderItem* const> items,
			const std::vector<ParticleCustomParameterLayout>& customLayouts,
			std::vector<ParticleDrawInstanceData>& outInstances, std::vector<uint32_t>& outPhaseCounts,
			std::vector<uint8_t>& outCustomParameters, std::vector<uint32_t>& outCustomOffsets) const;
		// パラメトリックMS生成で描画する、パイプラインを解決できなければfalse
		bool DrawParametricShapePath(const RenderDrawContext& context, const RenderItem* item,
			const IParticleParametricShape& parametric, const ParticleRenderSettings& settings,
			const BackendDrawCommon::ResolvedMaterialPass& resolvedPass,
			const std::unordered_map<std::string, MaterialParameterValue>* materialOverrides,
			D3D12_GPU_VIRTUAL_ADDRESS geometryAddress, D3D12_GPU_VIRTUAL_ADDRESS materialsAddress,
			D3D12_GPU_VIRTUAL_ADDRESS customParametersAddress,
			uint32_t instanceCount,
			D3D12_GPU_VIRTUAL_ADDRESS viewAddress);
		// Model粒子をインスタンシング描画する、メッシュを解決できなければfalse
		bool DrawModelMeshPath(const RenderDrawContext& context, const RenderItem* item,
			const ParticleRenderSettings& settings,
			const BackendDrawCommon::ResolvedMaterialPass& resolvedPass,
			const std::unordered_map<std::string, MaterialParameterValue>* materialOverrides,
			D3D12_GPU_VIRTUAL_ADDRESS geometryAddress, D3D12_GPU_VIRTUAL_ADDRESS materialsAddress,
			D3D12_GPU_VIRTUAL_ADDRESS customParametersAddress,
			uint32_t instanceCount,
			D3D12_GPU_VIRTUAL_ADDRESS viewAddress);
		// 共有ジオメトリでインスタンシング描画する
		void DrawSharedGeometryPath(const RenderDrawContext& context, const RenderItem* item,
			const ParticleRenderSettings& settings,
			const BackendDrawCommon::ResolvedMaterialPass& resolvedPass,
			const std::unordered_map<std::string, MaterialParameterValue>* materialOverrides,
			D3D12_GPU_VIRTUAL_ADDRESS geometryAddress, D3D12_GPU_VIRTUAL_ADDRESS materialsAddress,
			D3D12_GPU_VIRTUAL_ADDRESS customParametersAddress,
			uint32_t instanceCount,
			D3D12_GPU_VIRTUAL_ADDRESS viewAddress);
		// トレイルを描画する
		void DrawTrails(const RenderDrawContext& context, const RenderItem* item,
			std::span<const RenderItem* const> items,
			const BackendDrawCommon::ResolvedMaterialPass& resolvedPass, ParticleBatchResources& resources);
	};
} // Engine
