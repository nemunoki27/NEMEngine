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

	// 共有リソースが無ければ描画不可
	if (!context.resources) {
		return;
	}

	// 不透明アイテムから深度に描画する対象のみ取得
	std::vector<const RenderItem*> items = CollectItems(context, passBuckets);
	RenderPassExecutionHelper::Execute(graphicsCore, context, items, deps_,
		context.resources->GetSceneMain(), MaterialPassKind::ZPrepass, false, true);
}

std::vector<const Engine::RenderItem*> Engine::DepthPrepass::CollectItems(
	const SceneExecutionContext& context, const RenderPassPhaseBuckets& passBuckets) const {

	// 不透明アイテムが空ならZPrepassの対象も無い
	std::vector<const RenderItem*> result{};
	const RenderPassItemList& list = passBuckets.Get(RenderPhase::Opaque);
	if (list.IsEmpty()) {
		return result;
	}
	// 深度は透視投影カメラ基準で書くため対応するカメラが無ければ描画不可
	const ResolvedCameraView* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	if (!camera) {
		return result;
	}

	result.reserve(list.items.size());
	for (const RenderItem* item : list.items) {

		if (!item) {
			continue;
		}
		// メッシュ以外は深度描画なし
		if (item->backendID != RenderBackendID::Mesh) {
			continue;
		}
		// カメラのカリングマスクで弾かれるレイヤーは除外する
		if ((item->visibilityLayerMask & camera->cullingMask) == 0) {
			continue;
		}
		// メッシュペイロードデータの中で深度描画が有効な場合のみ
		const MeshRenderPayload* payload = deps_.renderBatch->GetPayload<MeshRenderPayload>(*item);
		if (!payload || !payload->enableZPrepass) {
			continue;
		}
		result.emplace_back(item);
	}
	return result;
}
