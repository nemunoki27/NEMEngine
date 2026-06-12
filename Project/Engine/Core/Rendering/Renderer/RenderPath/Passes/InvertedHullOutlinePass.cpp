#include "InvertedHullOutlinePass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommandContext.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/World/Components/Rendering/InvertedHullOutlineComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

//============================================================================
//	InvertedHullOutlinePass classMethods
//============================================================================
void Engine::InvertedHullOutlinePass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	if (!context.resources) {
		return;
	}

	// 対象アイテムをstencil有無で振り分ける
	OutlineItemGroups groups = CollectItems(context, passBuckets);

	if (groups.regularItems.empty() && groups.stencilItems.empty()) {
		return;
	}

	MultiRenderTarget* sceneFinal = context.resources->GetSceneFinal();
	MultiRenderTarget* sceneMain = context.resources->GetSceneMain();
	if (!sceneFinal || !sceneMain) {
		return;
	}
	DepthTexture2D* sceneDepth = sceneMain->GetDepthTexture();

	// アウトラインHullはSceneFinalの色+ SceneMainの深度を組み合わせて描く
	RenderPassSurfaceBinding hullBinding{};
	hullBinding.colorSurface = sceneFinal;
	hullBinding.depthOverride = sceneDepth;

	// stencil抑制なしのHull描画でpreview等のVertex切り替えは既存挙動に任せ強制はしない
	if (!groups.regularItems.empty()) {

		RenderPassExecutionHelper::Execute(graphicsCore, context, groups.regularItems, deps_,
			hullBinding, MaterialPassKind::Outline, false, false);
	}

	// stencil抑制ありの描画
	if (!groups.stencilItems.empty()) {

		DxCommand* dxCommand = graphicsCore.GetDXObject().GetDxCommand();

		// SceneMainのstencilだけを0でclearし深度値は消さない
		if (sceneDepth) {

			sceneDepth->Transition(*dxCommand, D3D12_RESOURCE_STATE_DEPTH_WRITE);

			MultiRenderTargetClearDesc clear{};
			clear.clearColor = false;
			clear.clearDepth = false;
			clear.clearStencil = true;
			clear.clearStencilValue = 0;
			sceneMain->Clear(*dxCommand, clear);
		}

		ID3D12GraphicsCommandList6* commandList = dxCommand->GetCommandList();
		commandList->OMSetStencilRef(kOutlineStencilReference);

		// 元メッシュ形状でsilhouetteをstencilへREPLACE書き込みする(RTVなし/深度のみ)
		RenderPassExecutionHelper::Execute(graphicsCore, context, groups.stencilItems, deps_,
			sceneMain, MaterialPassKind::OutlineStencilWrite, false, true);

		// stencilがreferenceと異なる箇所だけHullを描いて内部や重なりを抑制する
		RenderPassExecutionHelper::Execute(graphicsCore, context, groups.stencilItems, deps_,
			hullBinding, MaterialPassKind::OutlineStencilTest, false, false);

		// 後続パスへ影響しないようstencil referenceを戻す
		commandList->OMSetStencilRef(0u);
	}
}

Engine::InvertedHullOutlinePass::OutlineItemGroups Engine::InvertedHullOutlinePass::CollectItems(
	const SceneExecutionContext& context, const RenderPassPhaseBuckets& passBuckets) const {

	// アウトラインはOpaqueの不透明メッシュにだけ付くのでそのバケットだけ見る
	OutlineItemGroups result{};
	const RenderPassItemList* list = passBuckets.Find(RenderPhase::Opaque);
	if (!list || list->IsEmpty()) {
		return result;
	}

	// 可視判定はPerspectiveカメラ基準
	const ResolvedCameraView* camera = context.view
		? context.view->FindCamera(RenderCameraDomain::Perspective)
		: nullptr;
	if (!camera) {
		return result;
	}

	result.regularItems.reserve(list->items.size());
	result.stencilItems.reserve(list->items.size());

	for (const RenderItem* item : list->items) {

		// メッシュかつworld参照を持つアイテムだけが対象
		if (!item || item->backendID != RenderBackendID::Mesh || !item->world) {
			continue;
		}
		if ((item->visibilityLayerMask & camera->cullingMask) == 0) {
			continue;
		}
		// Outlineコンポーネントが有効でwidthが正のものだけ採用する
		const auto* outline = item->world->TryGetComponent<InvertedHullOutlineComponent>(item->entity);
		if (!outline || !outline->enabled || outline->width <= 0.0f) {
			continue;
		}
		// stencil抑制の要否で2つのグループへ振り分ける
		(outline->useStencil ? result.stencilItems : result.regularItems).emplace_back(item);
	}
	return result;
}
