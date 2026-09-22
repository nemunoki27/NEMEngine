#include "UIElementSelection.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/UI/UIRuntimeService.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>
#include <Engine/Core/World/Components/UI/UIImageButtonComponent.h>
#include <Engine/Core/World/Components/UI/UITextButtonComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Context/EngineContext.h>
#include <Engine/Core/Platform/Input/InputSystem.h>

// c++
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace Engine::UIElementSelection {

	Engine::UUID GetLocalFileID(Engine::ECSWorld& world, Engine::Entity entity) {

		const auto* sceneObject = world.TryGetComponent<Engine::SceneObjectComponent>(entity);
		return sceneObject ? sceneObject->localFileID : Engine::UUID{};
	}

	Engine::Vector2 ResolveElementCenter(Engine::ECSWorld& world, Engine::Entity entity,
		const Engine::UIElementRuntime& runtime) {

		Engine::Vector2 localCenter{};
		if (const auto* sprite = world.TryGetComponent<Engine::SpriteRendererComponent>(entity)) {
			localCenter = Engine::Vector2(
				(0.5f - sprite->pivot.x) * sprite->size.x,
				(0.5f - sprite->pivot.y) * sprite->size.y);
		} else if (const auto* text = world.TryGetComponent<Engine::TextRendererComponent>(entity)) {
			const auto* layout =
				world.TryGetComponent<Engine::TextLayoutRuntimeComponent>(entity);
			const Engine::Vector2 boundsSize =
				layout ? layout->boundsSize : Engine::Vector2::AnyInit(0.0f);
			localCenter = Engine::Vector2(
				(0.5f - text->pivot.x) * boundsSize.x,
				(0.5f - text->pivot.y) * boundsSize.y);
		}
		const Engine::Vector3 center = Engine::Vector3::Transform(
			Engine::Vector3(localCenter.x, localCenter.y, 0.0f), runtime.screenMatrix);
		return Engine::Vector2(center.x, center.y);
	}

	UISelectableEntry* FindEntryByLocalFileID(Engine::ECSWorld& world,
		std::vector<UISelectableEntry>& entries, Engine::Entity canvas, Engine::UUID localFileID) {

		if (!localFileID) {
			return nullptr;
		}
		for (UISelectableEntry& entry : entries) {
			if (entry.canvas == canvas && GetLocalFileID(world, entry.entity) == localFileID) {
				return &entry;
			}
		}
		return nullptr;
	}

	UISelectableEntry* FindFirstInteractable(std::vector<UISelectableEntry>& entries, Engine::Entity canvas) {

		for (UISelectableEntry& entry : entries) {
			if (entry.canvas == canvas && entry.selectable->interactable) {
				return &entry;
			}
		}
		return nullptr;
	}

	UISelectableEntry* FindAutomaticNavigation(std::vector<UISelectableEntry>& entries,
		const UISelectableEntry& current, const Engine::Vector2& direction, bool wrap) {

		UISelectableEntry* best = nullptr;
		float bestScore = (std::numeric_limits<float>::max)();
		for (UISelectableEntry& candidate : entries) {

			if (candidate.entity == current.entity || candidate.canvas != current.canvas ||
				!candidate.selectable->interactable) {
				continue;
			}
			const Engine::Vector2 delta = candidate.center - current.center;
			const float forward = Engine::Vector2::Dot(delta, direction);
			if (forward <= 0.001f) {
				continue;
			}
			const float perpendicular = std::abs(delta.x * direction.y - delta.y * direction.x);
			const float score = forward + perpendicular * 2.0f;
			if (score < bestScore) {
				bestScore = score;
				best = &candidate;
			}
		}
		if (best || !wrap) {
			return best;
		}

		// 指定方向の反対端から最も近い要素へラップする
		float edge = (std::numeric_limits<float>::max)();
		for (UISelectableEntry& candidate : entries) {

			if (candidate.entity == current.entity || candidate.canvas != current.canvas ||
				!candidate.selectable->interactable) {
				continue;
			}
			const float projection = Engine::Vector2::Dot(candidate.center, direction);
			const Engine::Vector2 delta = candidate.center - current.center;
			const float perpendicular = std::abs(delta.x * direction.y - delta.y * direction.x);
			const float score = projection + perpendicular * 0.25f;
			if (score < edge) {
				edge = score;
				best = &candidate;
			}
		}
		return best;
	}

	bool IsTransitionTableEntry(Engine::ECSWorld& world, std::span<const Engine::CanvasNavigationCell> cells,
		const UISelectableEntry& entry) {

		const Engine::UUID localFileID = GetLocalFileID(world, entry.entity);
		return localFileID && std::find_if(cells.begin(), cells.end(),
			[localFileID](const Engine::CanvasNavigationCell& cell) {
				return cell.localFileID == localFileID;
			}) != cells.end();
	}

	UISelectableEntry* FindFirstTransitionTableEntry(Engine::ECSWorld& world,
		std::vector<UISelectableEntry>& entries, Engine::Entity canvasEntity,
		std::span<const Engine::CanvasNavigationCell> cells) {

		for (const Engine::CanvasNavigationCell& cell : cells) {

			UISelectableEntry* entry = FindEntryByLocalFileID(
				world, entries, canvasEntity, cell.localFileID);
			if (entry && entry->selectable->interactable) {
				return entry;
			}
		}
		return nullptr;
	}

	UISelectableEntry* FindTransitionTableNavigation(Engine::ECSWorld& world,
		std::vector<UISelectableEntry>& entries, Engine::Entity canvasEntity,
		const Engine::CanvasComponent& canvas, const UISelectableEntry& current, const Engine::Vector2& direction,
		std::span<const Engine::CanvasNavigationCell> cells) {

		const int32_t rows = canvas.navigationRows;
		const int32_t columns = canvas.navigationColumns;
		if (rows <= 0 || columns <= 0 || cells.empty()) {
			return nullptr;
		}

		const Engine::UUID currentLocalFileID = GetLocalFileID(world, current.entity);
		const auto currentCell = std::find_if(cells.begin(), cells.end(),
			[currentLocalFileID](const Engine::CanvasNavigationCell& cell) {
				return cell.localFileID == currentLocalFileID;
			});
		if (currentCell == cells.end()) {
			return FindFirstTransitionTableEntry(
				world, entries, canvasEntity, cells);
		}

		const int32_t currentIndex = static_cast<int32_t>(
			std::distance(cells.begin(), currentCell));
		int32_t row = currentIndex / columns;
		int32_t column = currentIndex % columns;
		const int32_t directionX = direction.x < 0.0f ? -1 : (0.0f < direction.x ? 1 : 0);
		const int32_t directionY = direction.y < 0.0f ? -1 : (0.0f < direction.y ? 1 : 0);
		const int32_t maxStep = directionX != 0 ? columns : rows;

		for (int32_t step = 0; step < maxStep; ++step) {

			row += directionY;
			column += directionX;
			if (canvas.wrapNavigation) {
				if (row < 0) { row = rows - 1; }
				else if (rows <= row) { row = 0; }
				if (column < 0) { column = columns - 1; }
				else if (columns <= column) { column = 0; }
			} else if (row < 0 || rows <= row || column < 0 || columns <= column) {
				return nullptr;
			}

			const size_t index = static_cast<size_t>(row * columns + column);
			if (cells.size() <= index) {
				continue;
			}
			UISelectableEntry* next = FindEntryByLocalFileID(
				world, entries, canvasEntity, cells[index].localFileID);
			if (next && next->entity != current.entity && next->selectable->interactable) {
				return next;
			}
		}
		return nullptr;
	}
}
