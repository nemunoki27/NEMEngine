#include "RuntimeScreenSpaceOutlinePass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/ScreenSpaceOutlineComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <cmath>
#include <unordered_set>

//============================================================================
//	RuntimeScreenSpaceOutlinePass classMethods
//============================================================================

namespace {

	uint64_t MakeEntityKey(const Engine::Entity& entity) {

		return (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
	}
}

void Engine::RuntimeScreenSpaceOutlinePass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	if (!context.resources) {
		return;
	}

	// Outlineコンポーネントが付いたEntityを集め、1件も無ければ描かない
	CollectRequests(context, passBuckets);
	if (requests_.empty()) {
		return;
	}

	// 集めた要求を専用rendererへ渡してruntime用のScreenSpaceOutlineへ描く
	renderer_.Render(graphicsCore, context, passBuckets, deps_, requests_,
		context.resources->GetRuntimeScreenSpaceOutline());
}

void Engine::RuntimeScreenSpaceOutlinePass::CollectRequests(
	const SceneExecutionContext& context, const RenderPassPhaseBuckets& passBuckets) {

	requests_.clear();

	// outline対象はOpaqueメッシュなのでそのバケットだけ走査する
	const RenderPassItemList& list = passBuckets.Get(RenderPhase::Opaque);
	if (list.IsEmpty()) {
		return;
	}

	// 同一Entityが複数サブメッシュで来ても二重登録しないよう既出を記録する
	std::unordered_set<uint64_t> visited{};
	visited.reserve(list.items.size());
	for (const RenderItem* item : list.items) {

		if (!item || item->backendID != RenderBackendID::Mesh || !item->world) {
			continue;
		}
		// 別worldのアイテムは対象にしない
		if (context.world && item->world != context.world) {
			continue;
		}
		if (!visited.emplace(MakeEntityKey(item->entity)).second) {
			continue;
		}

		const MeshRendererComponent* renderer = item->world->TryGetComponent<MeshRendererComponent>(item->entity);
		if (!renderer) {
			continue;
		}
		// Outlineが有効でwidthが有限の正値のものだけ採用する
		const ScreenSpaceOutlineComponent* outline = item->world->TryGetComponent<ScreenSpaceOutlineComponent>(item->entity);
		if (!outline || !outline->enabled || !std::isfinite(outline->widthPixels) || outline->widthPixels <= 0.0f) {
			continue;
		}
		// 階層的に非アクティブなEntityは描かない
		const SceneObjectComponent* sceneObject = item->world->TryGetComponent<SceneObjectComponent>(item->entity);
		if (sceneObject && !sceneObject->activeInHierarchy) {
			continue;
		}

		// コンポーネント値からEntity全体ぶんの描画要求を組み立てる
		ScreenSpaceOutlineRequest request{};
		request.world = item->world;
		request.entity = item->entity;
		request.subMeshIndex = -1;
		request.style.color = outline->color;
		request.style.widthPixels = outline->widthPixels;
		request.style.priority = outline->priority;
		request.style.visibilityMode = outline->visibilityMode;
		request.style.regionMode = outline->regionMode;
		request.source = ScreenSpaceOutlineSource::RuntimeComponent;
		requests_.emplace_back(request);
	}
}
