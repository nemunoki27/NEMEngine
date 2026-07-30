#include "LineRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>

//============================================================================
//	LineRenderBackend classMethods
//============================================================================
Engine::LineRenderBackend::~LineRenderBackend() {

	// FrameBatchResourcePool内のunique_ptrを終了時に明示resetする
	resourcePool_.Clear();
}

void Engine::LineRenderBackend::BeginFrame([[maybe_unused]] GraphicsCore& graphicsCore) {

	resourcePool_.BeginFrame();
	BeginFrameCommon();
}

void Engine::LineRenderBackend::AppendPolyline(const RenderItem& item, const LineRenderPayload& payload) {

	// 点を頂点へ変換する、ワールド絶対でなければitem.worldMatrixで変換する
	auto toVertex = [&](const LinePoint& point) {

		LineVertex vertex{};
		// worldMatrixは行ベクトル規約なのでTransformで適用する、TransformPointは列ベクトル規約で平行移動が壊れる
		vertex.position = payload.useWorldSpace ? point.position :
			Vector3::Transform(point.position, item.worldMatrix);
		// 2Dは正射影で奥行きを使わないのでzを潰す
		if (payload.is2D) {
			vertex.position.z = 0.0f;
		}
		vertex.thickness = point.thickness;
		vertex.color = point.color;
		return vertex;
		};

	// 線分リストは2点ずつ独立しているのでそのまま積む
	if (!payload.connected) {

		for (uint32_t i = 0; i < payload.pointCount; ++i) {
			lineScratch_.emplace_back(toVertex(payload.points[i]));
		}
		return;
	}

	// 連結ポリラインは隣り合う点をLINELISTの2頂点として積む
	for (uint32_t i = 0; i + 1 < payload.pointCount; ++i) {

		lineScratch_.emplace_back(toVertex(payload.points[i]));
		lineScratch_.emplace_back(toVertex(payload.points[i + 1]));
	}
	// loopは終点と始点をつなぐ
	if (payload.loop && payload.pointCount >= 3) {

		lineScratch_.emplace_back(toVertex(payload.points[payload.pointCount - 1]));
		lineScratch_.emplace_back(toVertex(payload.points[0]));
	}
}

void Engine::LineRenderBackend::DrawBatch(const RenderDrawContext& context,
	std::span<const RenderItem* const> items) {

	GraphicsCore& graphicsCore = *context.graphicsCore;

	// 描画に使うカメラを取得する、無効なら描画しない
	const ResolvedCameraView* camera = context.view->FindCamera(items.front()->cameraDomain);
	if (!camera || !camera->valid) {
		return;
	}

	// マテリアルパスを解決する
	BackendDrawCommon::ResolvedMaterialPass resolvedPass{};
	if (!BackendDrawCommon::ResolveMaterialPass(context, items.front()->material,
		DefaultMaterialSlot::Line, { MaterialPassKind::Draw }, resolvedPass)) {
		return;
	}
	// パイプラインを解決する
	const PipelineState* pipelineState = BackendDrawCommon::ResolveGraphicsPipeline(context, *resolvedPass.pass);
	if (!pipelineState) {
		return;
	}

	// 全アイテムのポリラインを線分リストへ展開する
	lineScratch_.clear();
	for (const RenderItem* item : items) {

		const LineRenderPayload* payload = context.batch->GetPayload<LineRenderPayload>(*item);
		if (!payload || payload->pointCount < 2 || payload->points == nullptr) {
			continue;
		}
		AppendPolyline(*item, *payload);
	}
	if (lineScratch_.empty()) {
		return;
	}

	// GPUリソースの更新
	LineBatchResources& resources = resourcePool_.Acquire(graphicsCore,
		[](LineBatchResources& resource, GraphicsCore& core) {
			resource.Init(core);
		});
	resources.UpdateView(*camera, *context.view);
	resources.UploadVertices(graphicsCore, lineScratch_);

	// パイプラインを設定
	ID3D12GraphicsCommandList6* commandList = BackendDrawCommon::SetupGraphicsPipeline(
		context, *pipelineState, items.front()->blendMode);

	// IAステージ設定
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
	commandList->IASetVertexBuffers(0, 1, &resources.GetVBV());

	// ルートパラメータをバインド
	{
		// レジストリのオートバインドとスロット解決をまとめて行う
		SyncAndBindRegistry(*pipelineState, context, commandList);
		if (perDrawBindCache_.Has(viewCBVSlot_)) {
			RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(viewCBVSlot_),
				resources.GetViewGPUAddress());
		}
		// overrides持ちはCanBatchで単独描画になるので先頭の上書きを使う
		const LineRenderPayload* firstPayload = context.batch->GetPayload<LineRenderPayload>(*items.front());
		if (resolvedPass.material) {
			BackendDrawCommon::BindReflectedMaterialParameters(context, materialParamBinder_, *pipelineState,
				*resolvedPass.material, firstPayload ? firstPayload->materialOverrides : nullptr,
				perDrawBindCache_, materialParamsCBVSlot_, commandList);
		}
		// space2のマテリアルテクスチャをreflection駆動でバインドする
		if (resolvedPass.material) {
			BackendDrawCommon::BindMaterialTextures(context, *pipelineState, materialParamBinder_,
				*resolvedPass.material, commandList);
		}
	}

	// 線分リストで描画
	commandList->DrawInstanced(resources.GetVertexCount(), 1, 0, 0);
}

bool Engine::LineRenderBackend::CanBatch(const RenderItem& first,
	const RenderItem& next, [[maybe_unused]] const GraphicsRuntimeFeatures& features) const {

	// カメラ種類が違う2Dと3Dは別バッチにする、混ぜると先頭のカメラで描いて片方が消える
	if (first.cameraDomain != next.cameraDomain) {
		return false;
	}
	// 同一マテリアル/ブレンドなら1本の頂点バッファへまとめる
	return BackendDrawCommon::CanBatchBasic(first, next);
}
