#include "UIRuntimeService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

// c++
#include <algorithm>
#include <cmath>
#include <functional>

//============================================================================
//	UIRuntimeService internal
//============================================================================
namespace {

	uint64_t MakeEntityKey(Engine::Entity entity) {

		return (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
	}

	bool IsActiveEntity(Engine::ECSWorld& world, Engine::Entity entity) {

		const auto* sceneObject = world.TryGetComponent<Engine::SceneObjectComponent>(entity);
		return !sceneObject || sceneObject->activeInHierarchy;
	}

	// アフィン行列が逆変換可能か判定する
	bool IsInvertibleAffine(const Engine::Matrix4x4& matrix) {

		const float determinant =
			matrix.m[0][0] * (matrix.m[1][1] * matrix.m[2][2] - matrix.m[1][2] * matrix.m[2][1]) -
			matrix.m[0][1] * (matrix.m[1][0] * matrix.m[2][2] - matrix.m[1][2] * matrix.m[2][0]) +
			matrix.m[0][2] * (matrix.m[1][0] * matrix.m[2][1] - matrix.m[1][1] * matrix.m[2][0]);
		return std::isfinite(determinant) && std::abs(determinant) > 0.000001f;
	}

	Engine::Matrix4x4 BuildCanvasMatrix(const Engine::CanvasComponent& canvas, const Engine::Vector2& viewportSize) {

		float scale = (std::max)(canvas.scaleFactor, 0.0001f);
		Engine::Vector2 offset{};
		if (canvas.scaleMode == Engine::CanvasScaleMode::ScaleWithScreenSize) {

			const float referenceWidth = (std::max)(canvas.referenceResolution.x, 1.0f);
			const float referenceHeight = (std::max)(canvas.referenceResolution.y, 1.0f);
			const float widthScale = (std::max)(viewportSize.x, 1.0f) / referenceWidth;
			const float heightScale = (std::max)(viewportSize.y, 1.0f) / referenceHeight;
			const float match = std::clamp(canvas.matchWidthOrHeight, 0.0f, 1.0f);
			const float resolutionScale = std::pow(2.0f,
				std::lerp(std::log2(widthScale), std::log2(heightScale), match));
			scale *= resolutionScale;

			const Engine::Vector2 contentSize = canvas.referenceResolution * scale;
			offset = (viewportSize - contentSize) * 0.5f;
		}

		const Engine::Matrix4x4 scaleMatrix = Engine::Matrix4x4::MakeScaleMatrix(
			Engine::Vector3(scale, scale, 1.0f));
		const Engine::Matrix4x4 translateMatrix = Engine::Matrix4x4::MakeTranslateMatrix(
			Engine::Vector3(offset.x, offset.y, 0.0f));
		return scaleMatrix * translateMatrix;
	}
}

//============================================================================
//	UIRuntimeService classMethods
//============================================================================
void Engine::UIRuntimeService::Build(ECSWorld& world, const Vector2& viewportSize) {

	WorldState state{};
	state.viewportSize = viewportSize;

	std::vector<Entity> canvases;
	world.ForEach<CanvasComponent>([&](Entity entity, const CanvasComponent& canvas) {

		if (canvas.enabled && IsActiveEntity(world, entity)) {
			canvases.emplace_back(entity);
		}
		});
	std::sort(canvases.begin(), canvases.end(), [&](Entity lhs, Entity rhs) {

		const auto& a = world.GetComponent<CanvasComponent>(lhs);
		const auto& b = world.GetComponent<CanvasComponent>(rhs);
		if (a.sortingLayer != b.sortingLayer) {
			return a.sortingLayer < b.sortingLayer;
		}
		if (a.order != b.order) {
			return a.order < b.order;
		}
		return lhs.index < rhs.index;
		});

	uint32_t hierarchyOrder = 0;
	for (Entity canvasEntity : canvases) {

		const auto& canvas = world.GetComponent<CanvasComponent>(canvasEntity);
		const Matrix4x4 canvasMatrix = BuildCanvasMatrix(canvas, viewportSize);
		std::function<void(Entity)> visit = [&](Entity entity) {

			if (!world.IsAlive(entity) || !IsActiveEntity(world, entity)) {
				return;
			}
			// ネストCanvasは自身の設定で別に走査する
			if (entity != canvasEntity && world.HasComponent<CanvasComponent>(entity)) {
				return;
			}

			UIElementRuntime element{};
			element.entity = entity;
			element.canvas = canvasEntity;
			element.canvasSortingLayer = canvas.sortingLayer;
			element.canvasOrder = canvas.order;
			element.hierarchyOrder = hierarchyOrder++;
			if (const auto* transform = world.TryGetComponent<TransformComponent>(entity)) {
				element.screenMatrix = transform->worldMatrix * canvasMatrix;
			} else {
				element.screenMatrix = canvasMatrix;
			}
			state.lookup[MakeEntityKey(entity)] = state.elements.size();
			state.elements.emplace_back(element);

			const auto* hierarchy = world.TryGetComponent<HierarchyComponent>(entity);
			Entity child = hierarchy ? hierarchy->firstChild : Entity::Null();
			while (world.IsAlive(child)) {
				const Entity next = world.GetComponent<HierarchyComponent>(child).nextSibling;
				visit(child);
				child = next;
			}
		};
		visit(canvasEntity);
	}

	worlds_[&world] = std::move(state);
}

void Engine::UIRuntimeService::Clear(ECSWorld& world) {

	worlds_.erase(&world);
	gameplayInputBlocked_ = false;
}

bool Engine::UIRuntimeService::TryScreenToLocalPoint(const ECSWorld& world, Entity canvas,
	const Vector2& screenPosition, Vector2& outLocalPosition) const {

	const UIElementRuntime* element = Find(world, canvas);
	if (!element || element->canvas != canvas || !IsInvertibleAffine(element->screenMatrix)) {
		return false;
	}

	const Vector3 localPosition = Vector3::Transform(
		Vector3(screenPosition.x, screenPosition.y, 0.0f),
		Matrix4x4::Inverse(element->screenMatrix));
	if (!std::isfinite(localPosition.x) || !std::isfinite(localPosition.y)) {
		return false;
	}
	outLocalPosition = Vector2(localPosition.x, localPosition.y);
	return true;
}

const Engine::UIElementRuntime* Engine::UIRuntimeService::Find(const ECSWorld& world, Entity entity) const {

	const auto worldIt = worlds_.find(&world);
	if (worldIt == worlds_.end()) {
		return nullptr;
	}
	const auto it = worldIt->second.lookup.find(MakeEntityKey(entity));
	return it != worldIt->second.lookup.end() ? &worldIt->second.elements[it->second] : nullptr;
}

const std::vector<Engine::UIElementRuntime>& Engine::UIRuntimeService::GetElements(const ECSWorld& world) const {

	static const std::vector<UIElementRuntime> empty;
	const auto it = worlds_.find(&world);
	return it != worlds_.end() ? it->second.elements : empty;
}

Engine::Vector2 Engine::UIRuntimeService::GetViewportSize(const ECSWorld& world) const {

	const auto it = worlds_.find(&world);
	return it != worlds_.end() ? it->second.viewportSize : Vector2{};
}

Engine::UIRuntimeService& Engine::UIRuntimeService::GetInstance() {

	static UIRuntimeService instance;
	return instance;
}
