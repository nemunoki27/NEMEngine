#include "CanvasComponent.h"

//============================================================================
//	include
//============================================================================
#include "CanvasComponentSerialization.h"
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <vector>

//============================================================================
//	CanvasComponent classMethods
//============================================================================
void Engine::CanvasComponent::OnAdded(ECSWorld& world, const Entity& entity, [[maybe_unused]] CanvasComponent& component) {

	// 入力割当と選択状態の保存先を用意する
	DynamicBuffer<CanvasInputBinding> bindings = world.AddBuffer<CanvasInputBinding>(entity);
	if (bindings.IsEmpty()) {
		for (const CanvasInputBinding& binding : CanvasComponentSerialization::GetDefaultInputBindings()) {
			bindings.Add(binding);
		}
	}

	DynamicBuffer<CanvasNavigationCell> cells = world.AddBuffer<CanvasNavigationCell>(entity);
	if (cells.IsEmpty()) {
		cells.Resize(9);
	}
	if (!world.HasComponent<CanvasRuntimeComponent>(entity)) {
		world.AddComponent<CanvasRuntimeComponent>(entity);
	}
}

void Engine::CanvasComponent::OnRemoved(ECSWorld& world, const Entity& entity) {

	// Canvasに付随するBufferと実行状態を取り除く
	if (world.HasBuffer<CanvasInputBinding>(entity)) {
		world.RemoveBuffer<CanvasInputBinding>(entity);
	}
	if (world.HasBuffer<CanvasNavigationCell>(entity)) {
		world.RemoveBuffer<CanvasNavigationCell>(entity);
	}
	if (world.HasComponent<CanvasRuntimeComponent>(entity)) {
		world.RemoveComponent<CanvasRuntimeComponent>(entity);
	}
}

void Engine::CanvasComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, [[maybe_unused]] CanvasComponent& component) {
}

void Engine::CanvasComponent::ReleaseStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, [[maybe_unused]] CanvasComponent& component) {
}

std::span<Engine::CanvasInputBinding> Engine::GetCanvasInputBindings(ECSWorld& world, const Entity& entity) {

	// 編集可能な入力割当を借用する
	return world.TryGetBuffer<CanvasInputBinding>(entity).GetSpan();
}

std::span<const Engine::CanvasInputBinding> Engine::GetCanvasInputBindings(const ECSWorld& world, const Entity& entity) {

	// 保存用の入力割当を読み取りだけで借用する
	return world.GetBufferSpan<CanvasInputBinding>(entity);
}

void Engine::SetCanvasInputBindings(ECSWorld& world, const Entity& entity, std::span<const CanvasInputBinding> bindings) {

	// 同じ入力設定を参照した置換も保護する
	world.SetBuffer<CanvasInputBinding>(entity, bindings);
}

std::span<Engine::CanvasNavigationCell> Engine::GetCanvasNavigationCells(ECSWorld& world, const Entity& entity) {

	// 編集可能な選択遷移セルを借用する
	return world.TryGetBuffer<CanvasNavigationCell>(entity).GetSpan();
}

std::span<const Engine::CanvasNavigationCell> Engine::GetCanvasNavigationCells(const ECSWorld& world, const Entity& entity) {

	// 保存用の選択遷移セルを読み取りだけで借用する
	return world.GetBufferSpan<CanvasNavigationCell>(entity);
}

void Engine::SetCanvasNavigationCells(ECSWorld& world, const Entity& entity, std::span<const UUID> cells) {

	// 参照IDを格納形式へ変換してから確定する
	std::vector<CanvasNavigationCell> converted;
	converted.reserve(cells.size());
	for (UUID cell : cells) {
		converted.push_back(CanvasNavigationCell{cell});
	}
	world.SetBuffer<CanvasNavigationCell>(entity, converted);
}
