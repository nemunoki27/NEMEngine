#include "FillMeshRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineGPUTypes.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Color.h>

//============================================================================
//	FillMeshRenderBackend internal
//============================================================================
namespace {

	// 描画で渡す定数バッファ
	struct FillMeshViewConstants {

		Engine::Matrix4x4 viewProjection = Engine::Matrix4x4::Identity();
		Engine::Vector3 cameraPos = Engine::Vector3::AnyInit(0.0f);
		float padding = 0.0f;
	};
	struct FillMeshObjectConstants {

		Engine::Matrix4x4 worldMatrix = Engine::Matrix4x4::Identity();
		Engine::Color4 color = Engine::Color4::White();
		uint32_t renderingLayerMask = 1u;
		uint32_t padding[3] = { 0, 0, 0 };
	};

	bool ResolveFillMeshPass(const Engine::RenderDrawContext& context, Engine::AssetID requestedMaterial,
		Engine::BackendDrawCommon::ResolvedMaterialPass& outResolved) {

		// 選択アウトラインのマスクは元マテリアルと切り離してFillMesh専用マスクマテリアルから解決する
		if (context.passKind == Engine::MaterialPassKind::ScreenSpaceOutlineMask ||
			context.passKind == Engine::MaterialPassKind::ScreenSpaceOutlineCoverageMask) {

			const Engine::AssetID materialID = Engine::BuiltinAssets::Materials::FillMeshOutlineMask;
			const Engine::MaterialAsset* material = context.assetLibrary->LoadMaterial(materialID);
			if (!material) {
				return false;
			}
			const Engine::MaterialPassBinding* pass = Engine::FindPass(*material, context.passKind);
			if (!pass) {
				return false;
			}
			outResolved.materialID = materialID;
			outResolved.material = material;
			outResolved.pass = pass;
			return true;
		}
		if (context.passKind == Engine::MaterialPassKind::Transparent) {
			if (Engine::BackendDrawCommon::ResolveMaterialPass(context, requestedMaterial,
				Engine::DefaultMaterialSlot::FillMesh, { Engine::MaterialPassKind::Transparent }, outResolved)) {
				return true;
			}
			return Engine::BackendDrawCommon::ResolveMaterialPass(context, Engine::AssetID{},
				Engine::DefaultMaterialSlot::FillMesh, { Engine::MaterialPassKind::Transparent }, outResolved);
		}
		return Engine::BackendDrawCommon::ResolveMaterialPass(context, requestedMaterial,
			Engine::DefaultMaterialSlot::FillMesh, { Engine::MaterialPassKind::Draw }, outResolved);
	}
}

//============================================================================
//	FillMeshRenderBackend classMethods
//============================================================================
Engine::FillMeshRenderBackend::~FillMeshRenderBackend() {

	resourcePool_.Clear();
}

void Engine::FillMeshRenderBackend::BeginFrame([[maybe_unused]] GraphicsCore& graphicsCore) {

	resourcePool_.BeginFrame();
	BeginFrameCommon();
}

void Engine::FillMeshRenderBackend::DrawBatch(const RenderDrawContext& context,
	std::span<const RenderItem* const> items) {

	GraphicsCore& graphicsCore = *context.graphicsCore;
	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();

	// 非インスタンシングなので先頭アイテムを描く
	const RenderItem* item = items.front();
	const FillMeshRenderPayload* payload = context.batch->GetPayload<FillMeshRenderPayload>(*item);
	if (!payload || !payload->positions || !payload->indices ||
		payload->positionCount == 0 || payload->indexCount == 0) {
		return;
	}

	// マテリアルパスとパイプラインを解決する
	BackendDrawCommon::ResolvedMaterialPass resolvedPass{};
	if (!ResolveFillMeshPass(context, item->material, resolvedPass)) {
		return;
	}
	const PipelineState* pipelineState = BackendDrawCommon::ResolveGraphicsPipeline(context, *resolvedPass.pass);
	if (!pipelineState) {
		return;
	}

	// 頂点を展開してアップロードする
	FillMeshBatchResources& resources = resourcePool_.Acquire(graphicsCore,
		[](FillMeshBatchResources& resource, GraphicsCore& core) {
			resource.Init(core);
		});
	resources.UploadVertices(
		std::span<const FillMeshPosition>(
			payload->positions, payload->positionCount),
		std::span<const FillMeshTriangleIndex>(
			payload->indices, payload->indexCount));
	if (resources.GetVertexCount() == 0) {
		return;
	}

	// viewとobjectの定数バッファを確保する
	FillMeshViewConstants viewConstants{};
	if (const ResolvedCameraView* camera = context.view->FindCamera(item->cameraDomain); camera && camera->valid) {
		viewConstants.viewProjection = camera->matrices.viewProjectionMatrix;
		viewConstants.cameraPos = camera->cameraPos;
	}
	const PostProcessConstantBufferAllocation viewAlloc = constantBufferAllocator_.AllocateAndUpload(device, viewConstants);

	FillMeshObjectConstants objectConstants{};
	// BillboardComponentがあればカメラへ向けたワールド行列に差し替える、影や反射でも常にメインカメラを向く
	const ResolvedRenderView* billboardView = context.billboardView ? context.billboardView : context.view;
	objectConstants.worldMatrix = billboardView ?
		RenderBillboard::ResolveWorldMatrix(*item, *billboardView) : item->worldMatrix;
	objectConstants.color = payload->color;
	objectConstants.renderingLayerMask =
		payload->renderingLayerMask;
	const PostProcessConstantBufferAllocation objectAlloc = constantBufferAllocator_.AllocateAndUpload(device, objectConstants);

	// パイプラインを設定する
	ID3D12GraphicsCommandList* commandList = BackendDrawCommon::SetupGraphicsPipeline(
		context, *pipelineState, item->blendMode);

	// ルートパラメータをバインドする
	SyncAndBindRegistry(*pipelineState, context, commandList);
	if (perDrawBindCache_.Has(viewCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(viewCBVSlot_), viewAlloc.gpuAddress);
	}
	if (perDrawBindCache_.Has(objectCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(objectCBVSlot_), objectAlloc.gpuAddress);
	}
	if (perDrawBindCache_.Has(verticesSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(verticesSRVSlot_),
			resources.GetVerticesGPUAddress(), {});
	}
	// reflection駆動のマテリアルパラメータ、cbuffer無のBuiltinは無回帰
	if (resolvedPass.material) {
		BindMaterial(context, *pipelineState, *resolvedPass.material, payload->materialInstance, commandList);
	}
	// 選択アウトラインのマスク描画ではStyle IDを渡す、通常描画は宣言が無いので無回帰
	if (perDrawBindCache_.Has(outlineMaskCBVSlot_)) {

		ScreenSpaceOutlineMaskConstants maskConstants{};
		maskConstants.styleID = context.screenSpaceOutlineMaskStyleID;
		maskConstants.restrictSubMeshIndex = context.screenSpaceOutlineMaskRestrictSubMeshIndex;
		const PostProcessConstantBufferAllocation maskAlloc = constantBufferAllocator_.AllocateAndUpload(device, maskConstants);
		RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(outlineMaskCBVSlot_), maskAlloc.gpuAddress);
	}

	// 頂点は展開済みなのでインデックス不要、三角形リストで描画する
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(resources.GetVertexCount(), 1, 0, 0);
}

bool Engine::FillMeshRenderBackend::CanBatch([[maybe_unused]] const RenderItem& first,
	[[maybe_unused]] const RenderItem& next, [[maybe_unused]] const GraphicsRuntimeFeatures& features) const {

	// 非インスタンシングなので常に単独描画にする
	return false;
}
