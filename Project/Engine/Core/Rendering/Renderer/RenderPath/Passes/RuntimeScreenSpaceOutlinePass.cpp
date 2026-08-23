#include "RuntimeScreenSpaceOutlinePass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderBackendCapabilities.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/World/Components/Rendering/ScreenSpaceOutlineComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

//============================================================================
//	RuntimeScreenSpaceOutlinePass classMethods
//============================================================================

namespace {

	constexpr std::array<Engine::RenderPhase, 2> kSceneOutlinePhases = {
		Engine::RenderPhase::Opaque,
		Engine::RenderPhase::Transparent,
	};
	constexpr std::array<Engine::RenderPhase, 1> kPostProcessMaskedUIOutlinePhases = {
		Engine::RenderPhase::PostProcessMaskedUI,
	};
	constexpr std::array<Engine::RenderPhase, 1> kScreenUIOutlinePhases = {
		Engine::RenderPhase::ScreenUI,
	};

	uint64_t MakeEntityKey(const Engine::Entity& entity) {

		return (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
	}
}

void Engine::RuntimeScreenSpaceOutlinePass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	if (!context.resources) {
		return;
	}
	if (scope_ != Scope::Scene) {
		ExecuteOrderedUI(graphicsCore, passBuckets, context);
		return;
	}
	const std::span<const RenderPhase> phases = kSceneOutlinePhases;

	// Outlineコンポーネントが付いたEntityを集め、1件も無ければ描かない
	CollectRequests(context, passBuckets, phases);
	if (requests_.empty()) {
		return;
	}
	MultiRenderTarget* compositeTarget = context.resources->GetSceneFinal();
	DepthTexture2D* depthOverride = nullptr;
	if (scope_ == Scope::Scene && context.resources->GetSceneMain()) {
		depthOverride = context.resources->GetSceneMain()->GetDepthTexture();
	}

	// 集めた要求を専用rendererへ渡してruntime用のScreenSpaceOutlineへ描く
	renderer_.Render(graphicsCore, context, passBuckets, deps_, requests_,
		context.resources->GetRuntimeScreenSpaceOutline(), phases,
		compositeTarget, depthOverride);
}

void Engine::RuntimeScreenSpaceOutlinePass::ExecuteOrderedUI(
	GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
	SceneExecutionContext& context) {

	const RenderPhase phase = scope_ == Scope::PostProcessMaskedUI ?
		RenderPhase::PostProcessMaskedUI : RenderPhase::ScreenUI;
	MultiRenderTarget* target = scope_ == Scope::PostProcessMaskedUI ?
		context.resources->GetSceneFinal() : context.defaultSurface;
	const RenderPassItemList& uiItems = passBuckets.Get(phase);
	if (!target || uiItems.IsEmpty()) {
		return;
	}

	const std::span<const RenderPhase> phases = scope_ == Scope::PostProcessMaskedUI ?
		std::span<const RenderPhase>(kPostProcessMaskedUIOutlinePhases) :
		std::span<const RenderPhase>(kScreenUIOutlinePhases);
	CollectRequests(context, passBuckets, phases);
	if (requests_.empty()) {
		DrawUIRange(graphicsCore, uiItems, context, target,
			0, uiItems.items.size());
		return;
	}

	// Entityが複数アイテムを持つ場合も全描画後にアウトラインを入れる
	std::unordered_map<uint64_t, size_t> lastItemIndices{};
	lastItemIndices.reserve(uiItems.items.size());
	for (size_t itemIndex = 0; itemIndex < uiItems.items.size(); ++itemIndex) {

		const RenderItem* item = uiItems.items[itemIndex];
		if (item) {
			lastItemIndices[MakeEntityKey(item->entity)] = itemIndex;
		}
	}

	scheduledUIRequests_.clear();
	requestBatchScratch_.clear();
	scheduledUIRequests_.reserve(requests_.size());
	requestBatchScratch_.reserve(requests_.size());
	for (const ScreenSpaceOutlineRequest& request : requests_) {

		if (request.uiOcclusionMode ==
			ScreenSpaceOutlineUIOcclusionMode::AlwaysVisible) {

			continue;
		}

		const auto found = lastItemIndices.find(MakeEntityKey(request.entity));
		if (found == lastItemIndices.end()) {
			continue;
		}
		scheduledUIRequests_.push_back({ found->second, request });
	}
	std::stable_sort(scheduledUIRequests_.begin(), scheduledUIRequests_.end(),
		[](const ScheduledUIRequest& lhs, const ScheduledUIRequest& rhs) {
			return lhs.afterItemIndex < rhs.afterItemIndex;
		});

	// 描画順を尊重する要求は対象直後へ合成し、後続UIに上書きさせる
	size_t drawBegin = 0;
	size_t scheduleBegin = 0;
	while (scheduleBegin < scheduledUIRequests_.size()) {

		const size_t afterItemIndex =
			scheduledUIRequests_[scheduleBegin].afterItemIndex;
		DrawUIRange(graphicsCore, uiItems, context, target,
			drawBegin, afterItemIndex + 1);
		drawBegin = afterItemIndex + 1;

		requestBatchScratch_.clear();
		size_t scheduleEnd = scheduleBegin;
		while (scheduleEnd < scheduledUIRequests_.size() &&
			scheduledUIRequests_[scheduleEnd].afterItemIndex == afterItemIndex) {

			requestBatchScratch_.emplace_back(
				scheduledUIRequests_[scheduleEnd].request);
			++scheduleEnd;
		}
		RenderUIRequests(graphicsCore, passBuckets, context,
			phase, target, requestBatchScratch_);
		scheduleBegin = scheduleEnd;
	}
	DrawUIRange(graphicsCore, uiItems, context, target,
		drawBegin, uiItems.items.size());

	// 常に手前の要求は全UI描画後にまとめて合成する
	requestBatchScratch_.clear();
	for (const ScreenSpaceOutlineRequest& request : requests_) {

		if (request.uiOcclusionMode ==
			ScreenSpaceOutlineUIOcclusionMode::AlwaysVisible) {

			requestBatchScratch_.emplace_back(request);
		}
	}
	RenderUIRequests(graphicsCore, passBuckets, context,
		phase, target, requestBatchScratch_);
}

void Engine::RuntimeScreenSpaceOutlinePass::DrawUIRange(
	GraphicsCore& graphicsCore, const RenderPassItemList& items,
	SceneExecutionContext& context, MultiRenderTarget* target,
	size_t beginIndex, size_t endIndex) {

	if (!target || beginIndex >= endIndex || endIndex > items.items.size()) {
		return;
	}
	uiDrawScratch_.assign(
		items.items.begin() + beginIndex,
		items.items.begin() + endIndex);
	RenderPassExecutionHelper::Execute(graphicsCore, context,
		uiDrawScratch_, deps_, target);
}

void Engine::RuntimeScreenSpaceOutlinePass::RenderUIRequests(
	GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
	SceneExecutionContext& context, RenderPhase phase, MultiRenderTarget* target,
	std::span<const ScreenSpaceOutlineRequest> requests) {

	if (!target || requests.empty()) {
		return;
	}
	const std::array<RenderPhase, 1> phases = { phase };
	renderer_.Render(graphicsCore, context, passBuckets, deps_, requests,
		context.resources->GetRuntimeScreenSpaceOutline(),
		phases, target, nullptr);
}

void Engine::RuntimeScreenSpaceOutlinePass::CollectRequests(
	const SceneExecutionContext& context, const RenderPassPhaseBuckets& passBuckets,
	std::span<const RenderPhase> phases) {

	requests_.clear();

	// 同一Entityが複数サブメッシュで来ても二重登録しないよう既出を記録する
	std::unordered_set<uint64_t> visited{};
	size_t itemCount = 0;
	for (RenderPhase phase : phases) {
		itemCount += passBuckets.Get(phase).items.size();
	}
	visited.reserve(itemCount);
	for (RenderPhase phase : phases) {

		const RenderPassItemList& list = passBuckets.Get(phase);
		for (const RenderItem* item : list.items) {

			// マスクパスを解決できるバックエンドだけを対象にする
			if (!item || !item->world ||
				!RenderBackendCapabilities::SupportsOutlineMask(item->backendID)) {
				continue;
			}
			// 別worldのアイテムは対象にしない
			if (context.world && item->world != context.world) {
				continue;
			}
			if (!visited.emplace(MakeEntityKey(item->entity)).second) {
				continue;
			}

			// Outlineが有効でwidthが有限の正値のものだけ採用する
			const ScreenSpaceOutlineComponent* outline =
				item->world->TryGetComponent<ScreenSpaceOutlineComponent>(item->entity);
			if (!outline || !outline->enabled ||
				!std::isfinite(outline->widthPixels) || outline->widthPixels <= 0.0f) {
				continue;
			}
			// 階層的に非アクティブなEntityは描かない
			const SceneObjectComponent* sceneObject =
				item->world->TryGetComponent<SceneObjectComponent>(item->entity);
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
			request.alphaSource = outline->alphaSource;
			request.uiOcclusionMode = outline->uiOcclusionMode;
			request.source = ScreenSpaceOutlineSource::RuntimeComponent;
			requests_.emplace_back(request);
		}
	}
}
