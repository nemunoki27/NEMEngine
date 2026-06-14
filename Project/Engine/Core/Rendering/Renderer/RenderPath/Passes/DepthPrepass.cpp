#include "DepthPrepass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>

//============================================================================
//	DepthPrepass classMethods
//============================================================================
void Engine::DepthPrepass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	// RenderPathが持つ共有リソースが無ければ描画対象も無いので抜ける
	if (!context.resources) {
		return;
	}

	// OpaqueバケットからZPrepass対象だけを集め、SceneMainの深度へ書き込む
	// UseMeshShaderの設定どおりにパスを選ばせるためVertexは強制しない
	std::vector<const RenderItem*> items = CollectItems(context, passBuckets);
	RenderPassExecutionHelper::Execute(graphicsCore, context, items, deps_,
		context.resources->GetSceneMain(), MaterialPassKind::ZPrepass, false, true);
}

std::vector<const Engine::RenderItem*> Engine::DepthPrepass::CollectItems(
	const SceneExecutionContext& context, const RenderPassPhaseBuckets& passBuckets) const {

	// Opaqueバケットが空ならZPrepassの対象も無い
	std::vector<const RenderItem*> result{};
	const RenderPassItemList* list = passBuckets.Find(RenderPhase::Opaque);
	if (!list || list->IsEmpty()) {
		return result;
	}
	// 深度はPerspectiveカメラ基準で書くため対応するカメラが無ければ抜ける
	const ResolvedCameraView* camera = context.view ? context.view->FindCamera(RenderCameraDomain::Perspective) : nullptr;
	if (!camera) {
		return result;
	}

	result.reserve(list->items.size());
	for (const RenderItem* item : list->items) {

		if (!item) {
			continue;
		}
		// メッシュ以外は深度プリパスへ出さない
		if (item->backendID != RenderBackendID::Mesh) {
			continue;
		}
		// カメラのcullingMaskで弾かれるレイヤーは除外する
		if ((item->visibilityLayerMask & camera->cullingMask) == 0) {
			continue;
		}
		// payloadがZPrepass有効を明示したアイテムだけを採用する
		const MeshRenderPayload* payload = deps_.renderBatch->GetPayload<MeshRenderPayload>(*item);
		if (!payload || !payload->enableZPrepass) {
			continue;
		}
		result.emplace_back(item);
	}
	return result;
}
