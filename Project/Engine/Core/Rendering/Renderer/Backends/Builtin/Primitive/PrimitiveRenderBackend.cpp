#include "PrimitiveRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Assets/RenderPipelineAsset.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveMeshGenerator.h>
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineGPUTypes.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

//============================================================================
//	PrimitiveRenderBackend internal
//============================================================================
namespace {

	// ピクセル側でライティング適用を切り替えるインスタンスフラグ
	constexpr uint32_t kInstanceFlagLighting = 1u << 1;
	constexpr uint32_t kInstanceFlagReceiveShadow = 1u << 2;
	constexpr uint32_t kInstanceFlagReceiveIBL = 1u << 3;
	constexpr uint32_t kInstanceFlagReceiveReflection = 1u << 4;

	// renderFlagsのうちピクセル側で参照するものをインスタンスフラグへ写す
	uint32_t ToInstanceFlags(Engine::MeshRenderFlags renderFlags) {

		uint32_t flags = 0;
		if (Engine::HasMeshRenderFlag(renderFlags, Engine::MeshRenderFlags::Lighting)) {
			flags |= kInstanceFlagLighting;
		}
		if (Engine::HasMeshRenderFlag(renderFlags, Engine::MeshRenderFlags::ReceiveShadow)) {
			flags |= kInstanceFlagReceiveShadow;
		}
		if (Engine::HasMeshRenderFlag(renderFlags, Engine::MeshRenderFlags::ReceiveIBL)) {
			flags |= kInstanceFlagReceiveIBL;
		}
		if (Engine::HasMeshRenderFlag(renderFlags, Engine::MeshRenderFlags::ReceiveReflection)) {
			flags |= kInstanceFlagReceiveReflection;
		}
		return flags;
	}

	// 描画で渡す定数バッファ
	struct PrimitiveViewConstants {

		Engine::Matrix4x4 viewProjection = Engine::Matrix4x4::Identity();
		Engine::Vector3 cameraPosition = Engine::Vector3::AnyInit(0.0f);
		float _viewPad0 = 0.0f;
	};
	// MeshShader経路でインデックス数を渡す定数バッファ
	struct PrimitiveMeshConstants {

		uint32_t indexCount = 0;
		uint32_t pad0 = 0;
		uint32_t pad1 = 0;
		uint32_t pad2 = 0;
	};

	// MeshShaderの1グループが担当する三角形数
	constexpr uint32_t kMeshGroupTriangles = 64;

	// 選択アウトラインのマスクは元マテリアルと切り離してPrimitive専用マスクマテリアルから解決する
	constexpr Engine::AssetID kOutlineMaskMaterial{ 0x70a1b2c3d4e5f610ull };

	bool ResolvePrimitivePass(const Engine::RenderDrawContext& context, Engine::AssetID requestedMaterial,
		Engine::BackendDrawCommon::ResolvedMaterialPass& outResolved) {

		if (context.passKind == Engine::MaterialPassKind::ScreenSpaceOutlineMask ||
			context.passKind == Engine::MaterialPassKind::ScreenSpaceOutlineCoverageMask) {

			const Engine::MaterialAsset* material = context.assetLibrary->LoadMaterial(kOutlineMaskMaterial);
			if (!material) {
				return false;
			}
			const Engine::MaterialPassBinding* pass = Engine::FindPass(*material, context.passKind);
			if (!pass) {
				return false;
			}
			outResolved.materialID = kOutlineMaskMaterial;
			outResolved.material = material;
			outResolved.pass = pass;
			return true;
		}
		if (context.passKind == Engine::MaterialPassKind::Transparent) {
			if (Engine::BackendDrawCommon::ResolveMaterialPass(context, requestedMaterial,
				Engine::DefaultMaterialSlot::Primitive, { Engine::MaterialPassKind::Transparent }, outResolved)) {
				return true;
			}
			return Engine::BackendDrawCommon::ResolveMaterialPass(context, Engine::AssetID{},
				Engine::DefaultMaterialSlot::Primitive, { Engine::MaterialPassKind::Transparent }, outResolved);
		}
		return Engine::BackendDrawCommon::ResolveMaterialPass(context, requestedMaterial,
			Engine::DefaultMaterialSlot::Primitive, { Engine::MaterialPassKind::Draw }, outResolved);
	}
}

//============================================================================
//	PrimitiveRenderBackend classMethods
//============================================================================
Engine::PrimitiveRenderBackend::~PrimitiveRenderBackend() {

	geometryManager_.Clear();
	resourcePool_.Clear();
}

void Engine::PrimitiveRenderBackend::BeginFrame(GraphicsCore& graphicsCore) {

	if (!geometryManagerInitialized_) {
		geometryManager_.Init(graphicsCore);
		geometryManagerInitialized_ = true;
	}
	geometryManager_.BeginFrame();
	resourcePool_.BeginFrame();
	constantBufferAllocator_.BeginFrame();
	materialParamBinder_.BeginFrame();
}

void Engine::PrimitiveRenderBackend::CollectInstances(const RenderDrawContext& context,
	std::span<const RenderItem* const> items, std::vector<PrimitiveInstanceData>& outInstances) const {

	outInstances.clear();
	outInstances.reserve(items.size());
	for (const RenderItem* item : items) {

		const PrimitiveRenderPayload* payload = context.batch->GetPayload<PrimitiveRenderPayload>(*item);
		if (!payload) {
			continue;
		}
		PrimitiveInstanceData instance{};
		// BillboardComponentがあればカメラへ向けたワールド行列に差し替える、影や反射でも常にメインカメラを向く
		const ResolvedRenderView* billboardView = context.billboardView ? context.billboardView : context.view;
		instance.worldMatrix = billboardView ?
			RenderBillboard::ResolveWorldMatrix(*item, *billboardView) : item->worldMatrix;
		instance.uvMatrix = payload->uvMatrix;
		instance.flags = payload->renderer ? ToInstanceFlags(payload->renderer->renderFlags) : 0;
		outInstances.emplace_back(instance);
	}
}

void Engine::PrimitiveRenderBackend::DrawBatch(const RenderDrawContext& context,
	std::span<const RenderItem* const> items) {

	GraphicsCore& graphicsCore = *context.graphicsCore;
	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();

	const RenderItem* item = items.front();
	const PrimitiveRenderPayload* payload = context.batch->GetPayload<PrimitiveRenderPayload>(*item);
	if (!payload || !payload->renderer) {
		return;
	}

	// マテリアルパスとパイプラインを解決する、解決バリアントでVS経路かMS経路かを分ける
	BackendDrawCommon::ResolvedMaterialPass resolvedPass{};
	if (!ResolvePrimitivePass(context, item->material, resolvedPass)) {
		return;
	}
	const PipelineVariantDesc* variant = nullptr;
	const PipelineState* pipelineState = BackendDrawCommon::ResolveGraphicsPipeline(context, *resolvedPass.pass, &variant);
	if (!pipelineState) {
		return;
	}
	const bool useMeshShader = variant && variant->kind == PipelineVariantKind::GraphicsMesh;

	// 形状ハッシュ単位の共有ジオメトリを取得する、無ければ生成する
	// batchKeyは上書き分離を含むためジオメトリ共有には形状ハッシュを使う
	const uint64_t geometryHash = PrimitiveMeshGenerator::ComputeHash(*payload->renderer);
	const PrimitiveGeometry* geometry = geometryManager_.GetOrCreate(graphicsCore, geometryHash, *payload->renderer);
	if (!geometry || geometry->indexCount == 0 || !geometry->vertexBuffer.buffer) {
		return;
	}

	// バッチのインスタンスデータを集めてアップロードする
	std::vector<PrimitiveInstanceData> instances;
	CollectInstances(context, items, instances);
	PrimitiveBatchResources& resources = resourcePool_.Acquire(graphicsCore,
		[](PrimitiveBatchResources& resource, GraphicsCore& core) {
			resource.Init(core);
		});
	resources.UploadInstances(instances);
	if (resources.GetInstanceCount() == 0) {
		return;
	}

	// viewの定数バッファを確保する
	PrimitiveViewConstants viewConstants{};
	if (const ResolvedCameraView* camera = context.view->FindCamera(item->cameraDomain); camera && camera->valid) {
		viewConstants.viewProjection = camera->matrices.viewProjectionMatrix;
		viewConstants.cameraPosition = camera->cameraPos;
	}
	const PostProcessConstantBufferAllocation viewAlloc = constantBufferAllocator_.AllocateAndUpload(device, viewConstants);

	// パイプラインを設定する
	ID3D12GraphicsCommandList6* commandList = BackendDrawCommon::SetupGraphicsPipeline(
		context, *pipelineState, item->blendMode);

	// ルートパラメータをバインドする
	registryAutoBindTable_.Sync(*pipelineState, *context.bufferRegistry);
	registryAutoBindTable_.BindGraphics(*context.bufferRegistry, commandList);

	perDrawBindCache_.Sync(*pipelineState);
	if (perDrawBindCache_.Has(viewCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(viewCBVSlot_), viewAlloc.gpuAddress);
	}
	if (perDrawBindCache_.Has(verticesSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(verticesSRVSlot_),
			geometry->vertexBuffer.buffer->GetResource()->GetGPUVirtualAddress(), {});
	}
	if (perDrawBindCache_.Has(instancesSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(instancesSRVSlot_),
			resources.GetInstancesGPUAddress(), {});
	}
	// reflection駆動のマテリアルパラメータとテクスチャ、宣言しないBuiltinは無回帰
	if (resolvedPass.material) {
		BackendDrawCommon::BindReflectedMaterialParameters(context, materialParamBinder_, *pipelineState,
			*resolvedPass.material, payload->materialOverrides,
			perDrawBindCache_, materialParamsCBVSlot_, commandList);
		BackendDrawCommon::BindMaterialTextures(context, *pipelineState, *resolvedPass.material,
			commandList, payload->materialOverrides);
	}
	// 選択アウトラインのマスク描画ではStyle IDを渡す、通常描画は宣言が無いので無回帰
	if (perDrawBindCache_.Has(outlineMaskCBVSlot_)) {

		ScreenSpaceOutlineMaskConstants maskConstants{};
		maskConstants.styleID = context.screenSpaceOutlineMaskStyleID;
		maskConstants.restrictSubMeshIndex = context.screenSpaceOutlineMaskRestrictSubMeshIndex;
		const PostProcessConstantBufferAllocation maskAlloc = constantBufferAllocator_.AllocateAndUpload(device, maskConstants);
		RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(outlineMaskCBVSlot_), maskAlloc.gpuAddress);
	}

	if (useMeshShader) {

		// MeshShader経路、インデックスSRVと三角形数を渡してDispatchMeshする
		PrimitiveMeshConstants meshConstants{};
		meshConstants.indexCount = geometry->indexCount;
		const PostProcessConstantBufferAllocation meshAlloc = constantBufferAllocator_.AllocateAndUpload(device, meshConstants);
		if (perDrawBindCache_.Has(meshConstantsCBVSlot_)) {
			RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(meshConstantsCBVSlot_), meshAlloc.gpuAddress);
		}
		if (perDrawBindCache_.Has(indicesSRVSlot_)) {
			RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(indicesSRVSlot_),
				geometry->indexSRV.buffer->GetResource()->GetGPUVirtualAddress(), {});
		}
		const uint32_t triangleCount = geometry->indexCount / 3;
		const uint32_t groupCount = (triangleCount + kMeshGroupTriangles - 1) / kMeshGroupTriangles;
		commandList->DispatchMesh(groupCount, resources.GetInstanceCount(), 1);
	} else {

		// VS経路、共有インデックスバッファでインスタンシング描画する
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		const D3D12_INDEX_BUFFER_VIEW indexBufferView = geometry->indexBuffer.GetIndexBufferView();
		commandList->IASetIndexBuffer(&indexBufferView);
		commandList->DrawIndexedInstanced(geometry->indexCount, resources.GetInstanceCount(), 0, 0, 0);
	}
}

bool Engine::PrimitiveRenderBackend::CanBatch(const RenderItem& first, const RenderItem& next,
	[[maybe_unused]] const GraphicsRuntimeFeatures& features) const {

	// 同一形状かつ同一マテリアルならインスタンシングでまとめる
	return BackendDrawCommon::CanBatchBasic(first, next);
}
