#include "ParticleRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include "ParticleDrawPreparation.h"
#include "ParticleDrawConstants.h"
#include "ParticleRenderDataUtility.h"
#include "ParticleTrailDataBuilder.h"

#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleMaterialCompatibility.h>
#include <Engine/Core/Rendering/Particle/Parametric/ParticleParametricShapeRegistry.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveMeshGenerator.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/BillboardComponent.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>
#include <unordered_map>

//============================================================================
//	ParticleRenderBackend internal
//============================================================================
using namespace Engine::ParticleDrawPreparation;

namespace {

	// 形状アニメのパラメトリックMS生成を解決する、MS対応GPUかつ登録済み形状のみ
	const Engine::IParticleParametricShape* ResolveParametricShape(
		const Engine::RenderDrawContext& context, const Engine::ParticleRenderSettings& settings) {

		if (!settings.shapeOverLifetime || settings.model ||
			settings.space == Engine::PrimitiveRenderSpace::Screen2D) {
			return nullptr;
		}
		if (!context.runtimeFeatures.useMeshShader || context.forceVertexMeshVariant) {
			return nullptr;
		}
		return Engine::ParticleParametricShapeRegistry::GetInstance().Find(settings.shape);
	}
}

//============================================================================
//	ParticleRenderBackend classMethods
//============================================================================
Engine::ParticleRenderBackend::~ParticleRenderBackend() {

	geometryManager_.Clear();
	meshResourceManager_.Finalize();
	resourcePool_.Clear();
}

void Engine::ParticleRenderBackend::PreloadMeshes(GraphicsCore& graphicsCore,
	AssetDatabase& assetDatabase, std::span<const AssetID> meshAssets) {

	if (!meshManagerInitialized_) {
		meshResourceManager_.Init(graphicsCore);
		meshManagerInitialized_ = true;
	}
	for (const AssetID& meshAssetID : meshAssets) {
		meshResourceManager_.RequestMesh(assetDatabase, meshAssetID);
	}
	meshResourceManager_.WaitAll();
}

void Engine::ParticleRenderBackend::BeginFrame(GraphicsCore& graphicsCore) {

	if (!geometryManagerInitialized_) {
		geometryManager_.Init(graphicsCore);
		geometryManagerInitialized_ = true;
	}
	if (!meshManagerInitialized_) {
		meshResourceManager_.Init(graphicsCore);
		meshManagerInitialized_ = true;
	}
	geometryManager_.BeginFrame();
	meshResourceManager_.BeginFrame(graphicsCore);
	resourcePool_.BeginFrame();
	BeginFrameCommon();
}

void Engine::ParticleRenderBackend::DrawTrails(const RenderDrawContext& context, const RenderItem* item,
	std::span<const RenderItem* const> items,
	const BackendDrawCommon::ResolvedMaterialPass& resolvedPass, ParticleBatchResources& resources) {

	GraphicsCore& graphicsCore = *context.graphicsCore;
	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();

	// トレイルVSまたはMSとMaterialのPS、描画状態を合成する
	const PipelineVariantKind desiredKind = context.runtimeFeatures.useMeshShader && !context.forceVertexMeshVariant ?
		PipelineVariantKind::GraphicsMesh : PipelineVariantKind::GraphicsVertex;
	const PipelineVariantDesc* variant = nullptr;
	const PipelineState* pipelineState = BackendDrawCommon::ResolveComposedGraphicsPipeline(
		context, *resolvedPass.pass, BuiltinAssets::Pipelines::ParticleTrail, desiredKind, &variant);
	if (!pipelineState) {
		return;
	}
	const auto resolveTexture = [&context](MaterialParameterSemantic semantic,
		const AssetID& textureAssetID) {
		return BackendDrawCommon::ResolveMaterialTextureIndex(
			context, semantic, textureAssetID);
	};
	const ParticleCustomParameterLayout customLayout =
		BuildParticleCustomParameterLayout(
			pipelineState->GetGraphicsReflection(),
			&resolvedPass.material->parameters, resolveTexture);
	ParticleTrailDataBuilder::Build(context, items, customLayout, trailDataScratch_);
	resources.UploadTrailGeometry(trailDataScratch_);
	if (resources.GetTrailSegmentCount() == 0) {
		return;
	}
	resources.UploadTrailMaterials(trailDataScratch_.materials);
	resources.UploadTrailCustomParameters(trailDataScratch_.customParameters);

	// viewの定数バッファを確保する
	ParticleViewConstants viewConstants{};
	if (const ResolvedCameraView* camera = context.view->FindCamera(item->cameraDomain); camera && camera->valid) {
		viewConstants.viewProjection = camera->matrices.viewProjectionMatrix;
		viewConstants.cameraPosition = camera->cameraPos;
	}
	const PostProcessConstantBufferAllocation viewAlloc = constantBufferAllocator_.AllocateAndUpload(device, viewConstants);
	ParticleTrailConstants trailConstants{};
	trailConstants.segmentCount = resources.GetTrailSegmentCount();
	const PostProcessConstantBufferAllocation trailAlloc =
		constantBufferAllocator_.AllocateAndUpload(device, trailConstants);

	ID3D12GraphicsCommandList6* commandList = BackendDrawCommon::SetupGraphicsPipeline(
		context, *pipelineState, item->blendMode);

	SyncAndBindRegistry(*pipelineState, context, commandList);
	if (perDrawBindCache_.Has(viewCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(viewCBVSlot_), viewAlloc.gpuAddress);
	}
	if (perDrawBindCache_.Has(trailConstantsCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(trailConstantsCBVSlot_),
			trailAlloc.gpuAddress);
	}
	if (perDrawBindCache_.Has(trailPointsSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(trailPointsSRVSlot_),
			resources.GetTrailPointsGPUAddress(), {});
	}
	if (perDrawBindCache_.Has(trailSegmentsSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(trailSegmentsSRVSlot_),
			resources.GetTrailSegmentsGPUAddress(), {});
	}
	if (perDrawBindCache_.Has(materialsSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(materialsSRVSlot_),
			resources.GetTrailMaterialsGPUAddress(), {});
	}
	if (perDrawBindCache_.Has(customParametersSRVSlot_) && resources.GetTrailCustomParametersGPUAddress() != 0) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(customParametersSRVSlot_),
			resources.GetTrailCustomParametersGPUAddress(), {});
	}
	if (resolvedPass.material) {

		MaterialParameterSet trailInstance{};
		const ParticleRenderPayload* payload = context.batch->GetPayload<ParticleRenderPayload>(*item);
		const ParticleGroupRuntimeState* group = payload ?
			ResolveParticleRenderGroup(*item, *payload) : nullptr;
		if (group) {
			BuildPhaseMaterialInstance(
				group->renderSettings.trail.materialSettings,
				trailInstance);
		}
		BindMaterial(context, *pipelineState, *resolvedPass.material,
			trailInstance.empty() ? nullptr : &trailInstance,
			commandList);
	}

	if (variant && variant->kind == PipelineVariantKind::GraphicsMesh) {

		// 点列から1グループ32セグメントずつGPUでリボンへ展開する
		const uint32_t groupCount =
			(resources.GetTrailSegmentCount() + kParticleTrailMeshGroupSegments - 1) /
			kParticleTrailMeshGroupSegments;
		commandList->DispatchMesh(groupCount, 1, 1);
		return;
	}

	// 非MS環境は1セグメントを1インスタンスとしてVSで展開する
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(6, resources.GetTrailSegmentCount(), 0, 0);
}

void Engine::ParticleRenderBackend::DrawBatch(const RenderDrawContext& context,
	std::span<const RenderItem* const> items) {

	GraphicsCore& graphicsCore = *context.graphicsCore;

	const RenderItem* item = items.front();
	const ParticleRenderPayload* payload = context.batch->GetPayload<ParticleRenderPayload>(*item);
	const ParticleGroupRuntimeState* group = payload ?
		ResolveParticleRenderGroup(*item, *payload) : nullptr;
	if (!group || (group->particles.empty() && group->trails.empty())) {
		return;
	}
	const ParticleRenderSettings& settings = group->renderSettings;

	const bool is2D = settings.space == PrimitiveRenderSpace::Screen2D;
	ParticleBatchResources& resources = resourcePool_.Acquire(graphicsCore,
		[](ParticleBatchResources& resource, GraphicsCore& core) {
			resource.Init(core);
		});

	// 元形状を描画する場合のみフェーズごとのインスタンスデータを構築する
	if ((!settings.trail.enabled || settings.trail.drawSource) && !group->particles.empty()) {

		const size_t phaseCount = (std::max)((std::max)(settings.phaseMaterials.size(),
			settings.phaseMaterialSettings.size()), static_cast<size_t>(1));
		std::vector<BackendDrawCommon::ResolvedMaterialPass> phasePasses(phaseCount);
		std::vector<ParticleCustomParameterLayout> customLayouts(phaseCount);
		const auto resolveTexture = [&context](MaterialParameterSemantic semantic,
			const AssetID& textureAssetID) {
			return BackendDrawCommon::ResolveMaterialTextureIndex(
				context, semantic, textureAssetID);
		};
		for (size_t phaseIndex = 0; phaseIndex < phaseCount; ++phaseIndex) {

			const AssetID phaseMaterial =
				(phaseIndex < settings.phaseMaterials.size() && settings.phaseMaterials[phaseIndex]) ?
				settings.phaseMaterials[phaseIndex] : settings.material;
			if (!ResolveParticlePass(context, phaseMaterial, is2D, phasePasses[phaseIndex])) {
				continue;
			}
			const PipelineState* reflectionPipeline = BackendDrawCommon::ResolveGraphicsPipeline(
				context, *phasePasses[phaseIndex].pass);
			if (reflectionPipeline) {
				customLayouts[phaseIndex] = BuildParticleCustomParameterLayout(
					reflectionPipeline->GetGraphicsReflection(),
					phasePasses[phaseIndex].material ?
						&phasePasses[phaseIndex].material->parameters : nullptr,
					resolveTexture);
			}
		}

		// バッチのインスタンスデータをフェーズごとに集めてアップロードする
		std::vector<ParticleDrawInstanceData> instances;
		std::vector<uint32_t> phaseCounts;
		std::vector<uint8_t> customParameters;
		std::vector<uint32_t> customOffsets;
		CollectParticleInstances(context, items, customLayouts, instances, phaseCounts, customParameters, customOffsets);
		resources.UploadInstances(instances);
		resources.UploadCustomParameters(customParameters);

		// viewの定数バッファを確保する
		ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();
		ParticleViewConstants viewConstants{};
		if (const ResolvedCameraView* camera = context.view->FindCamera(item->cameraDomain); camera && camera->valid) {
			viewConstants.viewProjection = camera->matrices.viewProjectionMatrix;
			viewConstants.cameraPosition = camera->cameraPos;
		}
		const PostProcessConstantBufferAllocation viewAlloc =
			constantBufferAllocator_.AllocateAndUpload(device, viewConstants);

		// フェーズごとにマテリアルを解決して連続範囲を描画する、未設定はエフェクト共通へ落とす
		// 形状アニメはパラメトリックMS、Model粒子はメッシュ、他は共有ジオメトリで描画する
		const IParticleParametricShape* parametric = ResolveParametricShape(context, settings);
		uint32_t instanceOffset = 0;
		for (size_t phaseIndex = 0; phaseIndex < phaseCounts.size(); ++phaseIndex) {

			const uint32_t instanceCount = phaseCounts[phaseIndex];
			if (instanceCount == 0) {
				continue;
			}
			const BackendDrawCommon::ResolvedMaterialPass& phasePass = phasePasses[phaseIndex];
			if (!phasePass.pass || !phasePass.material) {

				instanceOffset += instanceCount;
				continue;
			}
			MaterialParameterSet phaseInstance{};
			BuildPhaseMaterialInstance(
				GetPhaseMaterialSettings(settings, phaseIndex),
				phaseInstance);
			const MaterialParameterSet* phaseInstancePtr =
				phaseInstance.empty() ? nullptr : &phaseInstance;
			const D3D12_GPU_VIRTUAL_ADDRESS geometryAddress = resources.GetGeometryGPUAddress() +
				static_cast<uint64_t>(instanceOffset) * sizeof(ParticleGeometryData);
			const D3D12_GPU_VIRTUAL_ADDRESS materialsAddress = resources.GetMaterialsGPUAddress() +
				static_cast<uint64_t>(instanceOffset) * sizeof(ParticleMaterialData);
			const D3D12_GPU_VIRTUAL_ADDRESS customParametersAddress =
				customLayouts[phaseIndex].stride == 0 ? 0 :
				resources.GetCustomParametersGPUAddress() + customOffsets[phaseIndex];

			bool drawn = false;
			if (parametric) {
				drawn = DrawParametricShapePath(context, item, *parametric, settings, phasePass,
					phaseInstancePtr, geometryAddress, materialsAddress, customParametersAddress,
					instanceCount, viewAlloc.gpuAddress);
			}
			if (!drawn && settings.model) {
				drawn = DrawModelMeshPath(context, item, settings, phasePass,
					phaseInstancePtr, geometryAddress, materialsAddress, customParametersAddress,
					instanceCount, viewAlloc.gpuAddress);
			}
			if (!drawn) {
				DrawSharedGeometryPath(context, item, settings, phasePass,
					phaseInstancePtr, geometryAddress, materialsAddress, customParametersAddress,
					instanceCount, viewAlloc.gpuAddress);
			}
			instanceOffset += instanceCount;
		}
	}

	// トレイルは3Dのみリボンを構築して重ねて描画する、専用マテリアル未設定は粒子と同じものを使う
	if (settings.trail.enabled && !is2D) {

		const AssetID trailMaterial = settings.trail.material ? settings.trail.material : settings.material;
		BackendDrawCommon::ResolvedMaterialPass trailPass{};
		if (ResolveParticlePass(context, trailMaterial, is2D, trailPass)) {

			DrawTrails(context, item, items, trailPass, resources);
		}
	}
}

//============================================================================
//	ParticleRenderBackend classMethods
//============================================================================

namespace Engine {

	ParticleRenderBackend::ParticleRenderBackend() {

		shapeConstantsCBVSlot_ = perDrawBindCache_.AddSlot("ParticleShapeConstants", ShaderBindingKind::CBV);
		trailConstantsCBVSlot_ = perDrawBindCache_.AddSlot("ParticleTrailConstants", ShaderBindingKind::CBV);
		verticesSRVSlot_ = perDrawBindCache_.AddSlot("gVertices", ShaderBindingKind::SRV);
		geometrySRVSlot_ = perDrawBindCache_.AddSlot("gParticleGeometry", ShaderBindingKind::SRV);
		materialsSRVSlot_ = perDrawBindCache_.AddSlot("gParticleMaterials", ShaderBindingKind::SRV);
		customParametersSRVSlot_ = perDrawBindCache_.AddSlot("gParticleCustomParameters", ShaderBindingKind::SRV);
		trailPointsSRVSlot_ = perDrawBindCache_.AddSlot("gTrailPoints", ShaderBindingKind::SRV);
		trailSegmentsSRVSlot_ = perDrawBindCache_.AddSlot("gTrailSegments", ShaderBindingKind::SRV);
	}
}
