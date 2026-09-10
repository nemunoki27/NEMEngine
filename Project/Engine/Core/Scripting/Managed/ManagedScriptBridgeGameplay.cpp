#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Audio/AudioSourceComponent.h>
#include <Engine/Core/World/Components/Rendering/LineRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>
#include <Engine/Core/World/Components/UI/UIProgressComponent.h>
#include <Engine/Core/World/Components/UI/UIImageButtonComponent.h>
#include <Engine/Core/World/Components/UI/UITextButtonComponent.h>
#include <Engine/Core/World/UI/UIRuntimeService.h>
#include <Engine/Core/Rendering/Meshes/Animation/SkinnedMeshAnimationManager.h>
#include <Engine/Core/Rendering/Core/RenderingPlatform.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineImmediateBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineShapeBuilder.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Engine {

	//============================================================================
	//	ゲームプレイの構造変更コールバック
	//	Prefabは返却前に実体化し、その他の走査を壊す構造変更はWorldCommandBufferへ積む
	//============================================================================
	namespace {

		// コールバック中に対象worldを解決する、parentが有効ならそのworld無効なら現在のactive world
		ECSWorld* ResolveTargetWorld(ManagedNativeEntity parent) {

			if (ECSWorld* fromParent = ResolveWorld(parent)) {
				return fromParent;
			}
			const SystemContext* context = ManagedScriptRuntime::GetCurrentContext();
			return context ? context->world : nullptr;
		}

		bool IsCanvasBindingCategoryValid(int32_t action, int32_t device) {

			return 0 <= action &&
				action <= static_cast<int32_t>(CanvasInputAction::Submit) &&
				0 <= device &&
				device <= static_cast<int32_t>(CanvasInputDevice::Gamepad);
		}
	}

	ManagedNativeEntity ManagedScriptRuntime::ResolveEntityRefCallback(
		ManagedAssetGUID sourceAsset, uint64_t localFileID) {

		// localFileIDとsourceAssetから現在のworldのentityを引く
		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : currentReferenceWorld_;
		if (!world || localFileID == 0) {
			return MakeNullNativeEntity();
		}

		const SceneInstanceManager* sceneInstances =
			world->GetCommandServices().sceneInstances;
		const SceneInstance* activeScene = sceneInstances ? sceneInstances->GetActive() : nullptr;
		const UUID activeSceneInstanceID = activeScene ? activeScene->instanceID : UUID{};

		const AssetID sourceAssetID = ToAssetID(sourceAsset);
		Entity fallback = Entity::Null();
		Entity sourceMatch = Entity::Null();
		Entity activeMatch = Entity::Null();
		Entity activeSourceMatch = Entity::Null();
		world->ForEach<SceneObjectComponent>([&](Entity entity,
			SceneObjectComponent& sceneObject) {

			if (sceneObject.localFileID.value != localFileID) {
				return;
			}
			if (!world->IsAlive(fallback)) {
				fallback = entity;
			}
			const bool sourceMatched = !sourceAssetID ||
				sceneObject.sourceAsset == sourceAssetID;
			const bool activeMatched = activeSceneInstanceID &&
				sceneObject.sceneInstanceID == activeSceneInstanceID;
			if (sourceMatched && !world->IsAlive(sourceMatch)) {
				sourceMatch = entity;
			}
			if (activeMatched && !world->IsAlive(activeMatch)) {
				activeMatch = entity;
			}
			if (sourceMatched && activeMatched && !world->IsAlive(activeSourceMatch)) {
				activeSourceMatch = entity;
			}
			});

		const Entity entity = world->IsAlive(activeSourceMatch) ? activeSourceMatch :
			world->IsAlive(sourceMatch) ? sourceMatch :
			world->IsAlive(activeMatch) ? activeMatch : fallback;
		if (!world->IsAlive(entity)) {
			return MakeNullNativeEntity();
		}
		return MakeNativeEntity(*world, entity);
	}

	void ManagedScriptRuntime::GetEntityReferenceIdentityCallback(ManagedNativeEntity entity,
		ManagedAssetGUID* sourceAsset, uint64_t* localFileID, int32_t* kind) {

		if (sourceAsset) { *sourceAsset = {}; }
		if (localFileID) { *localFileID = 0; }
		if (kind) { *kind = 0; }

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved)) {
			return;
		}
		const SceneObjectComponent* sceneObject = world->TryGetComponent<SceneObjectComponent>(resolved);
		if (!sceneObject || !sceneObject->localFileID) {
			return;
		}
		if (sourceAsset) { *sourceAsset = ToManagedAssetGUID(sceneObject->sourceAsset); }
		if (localFileID) { *localFileID = sceneObject->localFileID.value; }
		// runtime worldのentityはScene由来として扱う
		if (kind) { *kind = 1; }
	}

	namespace {

		// ManagedLinePointをエンジンのLinePointへ変換する
		LinePoint ToLinePoint(const ManagedLinePoint& src) {

			LinePoint point{};
			point.position = Vector3(src.position.x, src.position.y, src.position.z);
			point.color = Color4(src.color.r, src.color.g, src.color.b, src.color.a);
			point.thickness = src.thickness;
			return point;
		}
	}

	void ManagedScriptRuntime::LineSetPointsCallback(ManagedNativeEntity entity,
		const ManagedLinePoint* points, int32_t count, int32_t loop) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return;
		}
		const Entity resolved = ResolveEntity(entity);
		LineRendererComponent* line = world->IsAlive(resolved) ?
			world->TryGetComponent<LineRendererComponent>(resolved) : nullptr;
		if (!line) {
			return;
		}

		// Managed配列を一度変換し、DynamicBufferをまとめて差し替える
		std::vector<LinePoint> converted{};
		if (points != nullptr && count > 0) {

			converted.reserve(static_cast<size_t>(count));
			for (int32_t i = 0; i < count; ++i) {
				converted.emplace_back(ToLinePoint(points[i]));
			}
		}
		SetLinePoints(*world, resolved, converted);
		line->loop = (loop != 0);
	}

	int32_t ManagedScriptRuntime::GetUISelectableRuntimeStateCallback(
		ManagedNativeEntity entity,
		ManagedUISelectableRuntimeState* outState) {

		if (!outState) {
			return 0;
		}
		*outState = {};

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		const UISelectableRuntimeComponent* runtime =
			world && world->IsAlive(resolved) ?
			world->TryGetComponent<UISelectableRuntimeComponent>(resolved) :
			nullptr;
		if (!runtime) {
			return 0;
		}

		outState->state = static_cast<int32_t>(runtime->state);
		outState->normalThisFrame = runtime->normalThisFrame ? 1 : 0;
		outState->selectedThisFrame = runtime->selectedThisFrame ? 1 : 0;
		outState->submittedThisFrame =
			runtime->submittedThisFrame ? 1 : 0;
		outState->disabledThisFrame =
			runtime->disabledThisFrame ? 1 : 0;
		return 1;
	}

	int32_t ManagedScriptRuntime::GetUIProgressRuntimeStateCallback(
		ManagedNativeEntity entity,
		ManagedUIProgressRuntimeState* outState) {

		if (!outState) {
			return 0;
		}
		*outState = {};

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		const UIProgressRuntimeData* runtime =
			world && world->IsAlive(resolved) ?
			TryGetUIProgressRuntime(*world, resolved) : nullptr;
		if (!runtime) {
			return 0;
		}

		// 外部Runtimeストレージから固定長スナップショットだけを渡す
		outState->displayedValue = runtime->displayedValue;
		outState->delayedValue = runtime->delayedValue;
		outState->initialized = runtime->initialized ? 1 : 0;
		return 1;
	}

	int32_t ManagedScriptRuntime::GetCanvasInputLockedCallback(
		ManagedNativeEntity entity) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		const CanvasRuntimeComponent* runtime =
			world && world->IsAlive(resolved) ?
			world->TryGetComponent<CanvasRuntimeComponent>(resolved) :
			nullptr;
		return runtime && runtime->inputLocked ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::GetUIButtonClickedCallback(
		ManagedNativeEntity entity, int32_t buttonType) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved)) {
			return 0;
		}
		if (buttonType == 0) {
			const UIImageButtonRuntimeComponent* runtime =
				world->TryGetComponent<UIImageButtonRuntimeComponent>(resolved);
			return runtime && runtime->clickedThisFrame ? 1 : 0;
		}
		if (buttonType == 1) {
			const UITextButtonRuntimeComponent* runtime =
				world->TryGetComponent<UITextButtonRuntimeComponent>(resolved);
			return runtime && runtime->clickedThisFrame ? 1 : 0;
		}
		return 0;
	}

	int32_t ManagedScriptRuntime::CanvasCopyInputBindingsCallback(
		ManagedNativeEntity entity, int32_t action, int32_t device,
		int32_t* bindings, int32_t capacity) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return 0;
		}
		const Entity resolved = ResolveEntity(entity);
		if (!world->IsAlive(resolved) ||
			!world->HasComponent<CanvasComponent>(resolved) ||
			!IsCanvasBindingCategoryValid(action, device)) {
			return 0;
		}

		const CanvasInputAction targetAction =
			static_cast<CanvasInputAction>(action);
		const CanvasInputDevice targetDevice =
			static_cast<CanvasInputDevice>(device);
		const std::span<const CanvasInputBinding> stored =
			GetCanvasInputBindings(*world, resolved);
		int32_t count = 0;
		for (const CanvasInputBinding& binding : stored) {
			if (binding.action == targetAction &&
				binding.device == targetDevice) {
				if (bindings && count < capacity) {
					bindings[count] = binding.code;
				}
				++count;
			}
		}
		return count;
	}

	void ManagedScriptRuntime::CanvasSetInputBindingsCallback(
		ManagedNativeEntity entity, int32_t action, int32_t device,
		const int32_t* bindings, int32_t count) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world || !IsCanvasBindingCategoryValid(action, device) ||
			count < 0) {
			return;
		}
		const Entity resolved = ResolveEntity(entity);
		if (!world->IsAlive(resolved) ||
			!world->HasComponent<CanvasComponent>(resolved)) {
			return;
		}

		const CanvasInputAction targetAction =
			static_cast<CanvasInputAction>(action);
		const CanvasInputDevice targetDevice =
			static_cast<CanvasInputDevice>(device);
		const int32_t minCode =
			targetDevice == CanvasInputDevice::Keyboard ? 1 : 0;
		const int32_t maxCode =
			targetDevice == CanvasInputDevice::Keyboard ?
			255 : static_cast<int32_t>(GamePadButtons::Counts) - 1;

		std::vector<CanvasInputBinding> replaced;
		const std::span<const CanvasInputBinding> stored =
			GetCanvasInputBindings(*world, resolved);
		replaced.reserve(stored.size() + static_cast<size_t>(count));
		for (const CanvasInputBinding& binding : stored) {
			if (binding.action != targetAction ||
				binding.device != targetDevice) {
				replaced.emplace_back(binding);
			}
		}
		for (int32_t i = 0; bindings && i < count; ++i) {

			const int32_t code = bindings[i];
			if (code < minCode || maxCode < code) {
				continue;
			}
			const bool duplicated = std::any_of(
				replaced.begin(), replaced.end(),
				[&](const CanvasInputBinding& binding) {
					return binding.code == code &&
						binding.action == targetAction &&
						binding.device == targetDevice;
				});
			if (!duplicated) {
				replaced.emplace_back(CanvasInputBinding{
					static_cast<uint16_t>(code), targetAction, targetDevice
					});
			}
		}
		SetCanvasInputBindings(*world, resolved, replaced);
	}

	int32_t ManagedScriptRuntime::CanvasGetNavigationTableSizeCallback(
		ManagedNativeEntity entity, int32_t* outRows, int32_t* outColumns) {

		if (!outRows || !outColumns) {
			return static_cast<int32_t>(CanvasNavigationTableResult::InvalidSize);
		}
		*outRows = 0;
		*outColumns = 0;

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved) ||
			!world->HasComponent<CanvasComponent>(resolved)) {
			return static_cast<int32_t>(CanvasNavigationTableResult::InvalidCanvas);
		}
		const auto& canvas = world->GetComponent<CanvasComponent>(resolved);
		*outRows = canvas.navigationRows;
		*outColumns = canvas.navigationColumns;
		return static_cast<int32_t>(CanvasNavigationTableResult::Success);
	}

	int32_t ManagedScriptRuntime::CanvasResizeNavigationTableCallback(
		ManagedNativeEntity entity, int32_t rows, int32_t columns) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return static_cast<int32_t>(CanvasNavigationTableResult::InvalidCanvas);
		}
		return static_cast<int32_t>(
			ResizeCanvasNavigationTable(*world, resolved, rows, columns));
	}

	int32_t ManagedScriptRuntime::CanvasGetNavigationCellCallback(
		ManagedNativeEntity entity, int32_t row, int32_t column,
		ManagedNativeEntity* outTarget) {

		if (!outTarget) {
			return static_cast<int32_t>(CanvasNavigationTableResult::InvalidTarget);
		}
		*outTarget = MakeNullNativeEntity();

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return static_cast<int32_t>(CanvasNavigationTableResult::InvalidCanvas);
		}

		Entity target = Entity::Null();
		const CanvasNavigationTableResult result =
			GetCanvasNavigationCell(*world, resolved, row, column, target);
		if (result == CanvasNavigationTableResult::Success && world->IsAlive(target)) {
			*outTarget = MakeNativeEntity(*world, target);
		}
		return static_cast<int32_t>(result);
	}

	int32_t ManagedScriptRuntime::CanvasSetNavigationCellCallback(
		ManagedNativeEntity entity, int32_t row, int32_t column,
		ManagedNativeEntity target) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return static_cast<int32_t>(CanvasNavigationTableResult::InvalidCanvas);
		}

		Entity resolvedTarget = Entity::Null();
		if (target.IsValid()) {
			ECSWorld* targetWorld = ResolveWorld(target);
			resolvedTarget = ResolveEntity(target);
			if (targetWorld != world || !world->IsAlive(resolvedTarget)) {
				return static_cast<int32_t>(CanvasNavigationTableResult::InvalidTarget);
			}
		}
		return static_cast<int32_t>(
			SetCanvasNavigationCell(*world, resolved, row, column, resolvedTarget));
	}

	int32_t ManagedScriptRuntime::CanvasScreenToLocalPointCallback(
		ManagedNativeEntity entity, ManagedVector2 screenPosition,
		ManagedVector2* outLocalPosition) {

		if (!outLocalPosition) {
			return 0;
		}

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved) ||
			!world->HasComponent<CanvasComponent>(resolved)) {
			return 0;
		}

		Vector2 localPosition{};
		if (!UIRuntimeService::GetInstance().TryScreenToLocalPoint(
			*world, resolved, Vector2(screenPosition.x, screenPosition.y), localPosition)) {
			return 0;
		}
		*outLocalPosition = ToManagedVector2(localPosition);
		return 1;
	}

	namespace {

		enum class ParticleSystemOperation :
			int32_t {

			Play,
			Pause,
			Stop,
			Clear,
			PlayOneShot,
		};

		enum class ParticleSystemStateQuery :
			int32_t {

			Playing,
			Emitting,
			Paused,
			Stopped,
			Alive,
			ParticleCount,
		};

		template<typename Function>
		void ForEachParticleSystem(ECSWorld& world, const Entity& root,
			bool withChildren, Function&& function) {

			if (!withChildren) {
				if (world.HasComponent<ParticleSystemComponent>(root)) {
					function(root);
				}
				return;
			}

			for (const Entity& entity :
				HierarchyUtility::CollectLogicalSubtree(world, root)) {

				if (world.HasComponent<ParticleSystemComponent>(entity)) {
					function(entity);
				}
			}
		}

		bool QueryParticleSystemState(const ECSWorld& world,
			const Entity& entity, ParticleSystemStateQuery state) {

			switch (state) {
			case ParticleSystemStateQuery::Playing:
				return IsParticleSystemPlaying(world, entity);
			case ParticleSystemStateQuery::Emitting:
				return IsParticleSystemEmitting(world, entity);
			case ParticleSystemStateQuery::Paused:
				return IsParticleSystemPaused(world, entity);
			case ParticleSystemStateQuery::Stopped:
				return IsParticleSystemStopped(world, entity);
			case ParticleSystemStateQuery::Alive:
				return IsParticleSystemAlive(world, entity);
			case ParticleSystemStateQuery::ParticleCount:
				return 0 < GetParticleSystemParticleCount(world, entity);
			}
			return false;
		}
	}

	void ManagedScriptRuntime::ParticleSystemControlCallback(
		ManagedNativeEntity entity, int32_t operation,
		int32_t stopBehavior, int32_t withChildren) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity root = ResolveEntity(entity);
		if (!world || !world->IsAlive(root) ||
			operation < static_cast<int32_t>(ParticleSystemOperation::Play) ||
			static_cast<int32_t>(ParticleSystemOperation::PlayOneShot) < operation) {
			return;
		}

		const ParticleSystemOperation command =
			static_cast<ParticleSystemOperation>(operation);
		ForEachParticleSystem(*world, root, withChildren != 0,
			[&](const Entity& target) {

				switch (command) {
				case ParticleSystemOperation::Play:
					RequestParticleSystemPlay(*world, target);
					break;
				case ParticleSystemOperation::Pause:
					RequestParticleSystemPause(*world, target);
					break;
				case ParticleSystemOperation::Stop: {

					const ParticleSystemStopBehavior behavior = stopBehavior == 0 ?
						ParticleSystemStopBehavior::StopEmittingAndClear :
						ParticleSystemStopBehavior::StopEmitting;
					RequestParticleSystemStop(*world, target, behavior);
					break;
				}
				case ParticleSystemOperation::Clear:
					RequestParticleSystemClear(*world, target);
					break;
				case ParticleSystemOperation::PlayOneShot:
					RequestParticleSystemRestart(*world, target, true);
					break;
				}
			});
	}

	int32_t ManagedScriptRuntime::ParticleSystemStateCallback(
		ManagedNativeEntity entity, int32_t state, int32_t withChildren) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity root = ResolveEntity(entity);
		if (!world || !world->IsAlive(root) ||
			state < static_cast<int32_t>(ParticleSystemStateQuery::Playing) ||
			static_cast<int32_t>(ParticleSystemStateQuery::ParticleCount) < state) {
			return 0;
		}

		const ParticleSystemStateQuery query =
			static_cast<ParticleSystemStateQuery>(state);
		if (query == ParticleSystemStateQuery::ParticleCount) {

			int32_t particleCount = 0;
			ForEachParticleSystem(*world, root, withChildren != 0,
				[&](const Entity& target) {

					particleCount +=
						GetParticleSystemParticleCount(*world, target);
				});
			return particleCount;
		}

		bool matched = false;
		ForEachParticleSystem(*world, root, withChildren != 0,
			[&](const Entity& target) {

				matched |= QueryParticleSystemState(*world, target, query);
			});
		return matched ? 1 : 0;
	}

	namespace {

		template<typename Function>
		int32_t VisitRendererMaterialInstances(
			Engine::ECSWorld& world, const Engine::Entity& entity,
			Engine::ManagedRendererMaterialTarget target,
			int32_t subMeshIndex, Function&& function) {

			int32_t count = 0;
			auto visit = [&](Engine::MaterialParameterSet& materialInstance) {
				function(materialInstance);
				++count;
			};

			switch (target) {
			case Engine::ManagedRendererMaterialTarget::Mesh:
				if (!world.TryGetComponent<Engine::MeshRendererComponent>(entity)) {
					break;
				}
				if (const std::span<Engine::SubMeshMaterial> subMeshes =
					Engine::GetMeshSubMeshes(world, entity);
					subMeshIndex < 0) {

					for (Engine::SubMeshMaterial& subMesh : subMeshes) {
						visit(subMesh.materialInstance);
					}
				} else if (static_cast<size_t>(subMeshIndex) < subMeshes.size()) {
					visit(subMeshes[static_cast<size_t>(subMeshIndex)].materialInstance);
				}
				break;
			case Engine::ManagedRendererMaterialTarget::Sprite:
				if (Engine::SpriteRendererComponent* renderer =
					world.TryGetComponent<Engine::SpriteRendererComponent>(entity)) {

					visit(renderer->materialInstance);
				}
				break;
			case Engine::ManagedRendererMaterialTarget::Text:
				if (Engine::TextRendererComponent* renderer =
					world.TryGetComponent<Engine::TextRendererComponent>(entity)) {

					visit(renderer->materialInstance);
				}
				break;
			case Engine::ManagedRendererMaterialTarget::Primitive:
				if (Engine::PrimitiveRendererComponent* renderer =
					world.TryGetComponent<Engine::PrimitiveRendererComponent>(entity)) {

					visit(renderer->materialInstance);
				}
				break;
			case Engine::ManagedRendererMaterialTarget::Line:
				if (Engine::LineRendererComponent* renderer =
					world.TryGetComponent<Engine::LineRendererComponent>(entity)) {

					visit(renderer->materialInstance);
				}
				break;
			}
			return count;
		}

		bool DecodeMaterialParameterValue(
			const Engine::ManagedMaterialParameterValue& source,
			Engine::MaterialParameterValue& outValue) {

			std::array<float, 4> floatValues{};
			std::memcpy(floatValues.data(), &source.data0, sizeof(floatValues));
			switch (static_cast<Engine::ManagedMaterialParameterValueType>(source.type)) {
			case Engine::ManagedMaterialParameterValueType::Float:
				outValue.value = floatValues[0];
				return true;
			case Engine::ManagedMaterialParameterValueType::Vector2:
				outValue.value = Engine::Vector2(floatValues[0], floatValues[1]);
				return true;
			case Engine::ManagedMaterialParameterValueType::Vector3:
				outValue.value = Engine::Vector3(
					floatValues[0], floatValues[1], floatValues[2]);
				return true;
			case Engine::ManagedMaterialParameterValueType::Vector4:
				outValue.value = Engine::Vector4(
					floatValues[0], floatValues[1], floatValues[2], floatValues[3]);
				return true;
			case Engine::ManagedMaterialParameterValueType::Color:
				outValue.value = Engine::Color4(
					floatValues[0], floatValues[1], floatValues[2], floatValues[3]);
				return true;
			case Engine::ManagedMaterialParameterValueType::Texture: {
				Engine::ManagedAssetGUID asset{};
				std::memcpy(&asset, &source.data0, sizeof(asset));
				outValue.value = Engine::ToAssetID(asset);
				return true;
			}
			case Engine::ManagedMaterialParameterValueType::Int: {
				int32_t value = 0;
				std::memcpy(&value, &source.data0, sizeof(value));
				outValue.value = value;
				return true;
			}
			case Engine::ManagedMaterialParameterValueType::UInt: {
				uint32_t value = 0;
				std::memcpy(&value, &source.data0, sizeof(value));
				outValue.value = value;
				return true;
			}
			case Engine::ManagedMaterialParameterValueType::Bool: {
				int32_t value = 0;
				std::memcpy(&value, &source.data0, sizeof(value));
				outValue.value = value != 0;
				return true;
			}
			}
			return false;
		}

		bool EncodeMaterialParameterValue(
			const Engine::MaterialParameterValue& source,
			Engine::ManagedMaterialParameterValue& outValue) {

			outValue = {};
			auto writeFloats = [&](std::array<float, 4> values) {
				std::memcpy(&outValue.data0, values.data(), sizeof(values));
			};

			if (const float* floatValue = std::get_if<float>(&source.value)) {
				writeFloats({ *floatValue, 0.0f, 0.0f, 0.0f });
				outValue.type = static_cast<int32_t>(
					Engine::ManagedMaterialParameterValueType::Float);
			} else if (const Engine::Vector2* vector2 =
				std::get_if<Engine::Vector2>(&source.value)) {

				writeFloats({ vector2->x, vector2->y, 0.0f, 0.0f });
				outValue.type = static_cast<int32_t>(
					Engine::ManagedMaterialParameterValueType::Vector2);
			} else if (const Engine::Vector3* vector3 =
				std::get_if<Engine::Vector3>(&source.value)) {

				writeFloats({ vector3->x, vector3->y, vector3->z, 0.0f });
				outValue.type = static_cast<int32_t>(
					Engine::ManagedMaterialParameterValueType::Vector3);
			} else if (const Engine::Vector4* vector4 =
				std::get_if<Engine::Vector4>(&source.value)) {

				writeFloats({ vector4->x, vector4->y, vector4->z, vector4->w });
				outValue.type = static_cast<int32_t>(
					Engine::ManagedMaterialParameterValueType::Vector4);
			} else if (const Engine::Color4* color =
				std::get_if<Engine::Color4>(&source.value)) {

				writeFloats({ color->r, color->g, color->b, color->a });
				outValue.type = static_cast<int32_t>(
					Engine::ManagedMaterialParameterValueType::Color);
			} else if (const Engine::AssetID* assetID =
				std::get_if<Engine::AssetID>(&source.value)) {

				const Engine::ManagedAssetGUID asset =
					Engine::ToManagedAssetGUID(*assetID);
				std::memcpy(&outValue.data0, &asset, sizeof(asset));
				outValue.type = static_cast<int32_t>(
					Engine::ManagedMaterialParameterValueType::Texture);
			} else if (const int32_t* intValue =
				std::get_if<int32_t>(&source.value)) {

				std::memcpy(&outValue.data0, intValue, sizeof(*intValue));
				outValue.type = static_cast<int32_t>(
					Engine::ManagedMaterialParameterValueType::Int);
			} else if (const uint32_t* uintValue =
				std::get_if<uint32_t>(&source.value)) {

				std::memcpy(&outValue.data0, uintValue, sizeof(*uintValue));
				outValue.type = static_cast<int32_t>(
					Engine::ManagedMaterialParameterValueType::UInt);
			} else if (const bool* boolValue = std::get_if<bool>(&source.value)) {
				const int32_t native = *boolValue ? 1 : 0;
				std::memcpy(&outValue.data0, &native, sizeof(native));
				outValue.type = static_cast<int32_t>(
					Engine::ManagedMaterialParameterValueType::Bool);
			} else {
				return false;
			}
			return true;
		}

		// CollisionComponentの単一形状を取得する
		Engine::CollisionShape* ResolveCollisionShape(
			Engine::ECSWorld& world, const Engine::Entity& entity) {

			if (!world.IsAlive(entity)) {
				return nullptr;
			}
			Engine::CollisionComponent* collision = world.TryGetComponent<Engine::CollisionComponent>(entity);
			return collision ? &collision->shape : nullptr;
		}

		// Profile世代とPass UUIDを検証して現在のPassを返す
		const Engine::RenderFeaturePassSettings* ResolveRenderFeaturePass(
			uint64_t passID, uint64_t generation) {

			Engine::RenderFeatureProfileService& service =
				Engine::RenderFeatureProfileService::GetInstance();
			service.EnsureLoaded();
			if (generation == 0 ||
				generation != service.GetRuntimeGeneration()) {

				return nullptr;
			}
			return service.FindPassByID(Engine::UUID{ passID });
		}
	}

	int32_t ManagedScriptRuntime::SetRendererMaterialParameterCallback(
		ManagedNativeEntity entity, int32_t target, int32_t subMeshIndex,
		uint64_t parameterID, const char* name,
		const ManagedMaterialParameterValue* value) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world || !value || !name || name[0] == '\0') {
			return 0;
		}
		const Entity resolved = ResolveEntity(entity);
		if (!world->IsAlive(resolved)) {
			return 0;
		}

		MaterialParameterValue decoded{};
		if (!DecodeMaterialParameterValue(*value, decoded)) {
			return 0;
		}
		const std::string_view parameterName(name);
		const MaterialParameterID id{
			parameterID != 0 ?
			parameterID : MaterialParameterID::FromName(parameterName).value
		};
		const Color4* color = std::get_if<Color4>(&decoded.value);
		const bool colorOnly = target == static_cast<int32_t>(ManagedRendererMaterialTarget::Mesh) &&
			id == MaterialParameterIDs::BaseColor && parameterName == MaterialParameterNames::BaseColor && color;
		bool changed = false;
		const int32_t updated = VisitRendererMaterialInstances(
			*world, resolved,
			static_cast<ManagedRendererMaterialTarget>(target), subMeshIndex,
			[&](MaterialParameterSet& materialInstance) {
				if (colorOnly) {
					const auto* previous = materialInstance.Find(id);
					const auto* oldColor = previous ? std::get_if<Color4>(&previous->value) : nullptr;
					if (oldColor && oldColor->r == color->r && oldColor->g == color->g &&
						oldColor->b == color->b && oldColor->a == color->a) {
						return;
					}
				}
				materialInstance.Set(
					id, parameterName,
					ResolveMaterialParameterSemantic(parameterName), decoded);
				changed = true;
			});
		if (changed) {
			if (colorOnly) {
				world->MarkMeshColorModified(resolved);
			} else {
				world->MarkRenderDataModified(resolved);
			}
		}
		return updated;
	}

	int32_t ManagedScriptRuntime::GetRendererMaterialParameterCallback(
		ManagedNativeEntity entity, int32_t target, int32_t subMeshIndex,
		uint64_t parameterID, ManagedMaterialParameterValue* outValue) {

		if (!outValue || parameterID == 0) {
			return 0;
		}
		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return 0;
		}
		const Entity resolved = ResolveEntity(entity);
		if (!world->IsAlive(resolved)) {
			return 0;
		}

		bool found = false;
		VisitRendererMaterialInstances(
			*world, resolved,
			static_cast<ManagedRendererMaterialTarget>(target),
			subMeshIndex < 0 ? 0 : subMeshIndex,
			[&](MaterialParameterSet& materialInstance) {
				if (found) {
					return;
				}
				const MaterialParameterSet& readOnly =
					materialInstance;
				const MaterialParameterValue* value =
					readOnly.Find(MaterialParameterID{ parameterID });
				found = value && EncodeMaterialParameterValue(*value, *outValue);
			});
		return found ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::ClearRendererMaterialParameterCallback(
		ManagedNativeEntity entity, int32_t target, int32_t subMeshIndex,
		uint64_t parameterID) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world || parameterID == 0) {
			return 0;
		}
		const Entity resolved = ResolveEntity(entity);
		if (!world->IsAlive(resolved)) {
			return 0;
		}

		int32_t removed = 0;
		VisitRendererMaterialInstances(
			*world, resolved,
			static_cast<ManagedRendererMaterialTarget>(target), subMeshIndex,
			[&](MaterialParameterSet& materialInstance) {
				removed += static_cast<int32_t>(
					materialInstance.erase(MaterialParameterID{ parameterID }));
			});
		if (removed != 0) {
			world->MarkRenderDataModified();
		}
		return removed;
	}

	int32_t ManagedScriptRuntime::IsRayTracingSupportedCallback() {

		const SystemContext* context = GetCurrentContext();
		return context && context->graphicsPlatform &&
			context->graphicsPlatform->GetFeatureController().
			GetSupport().SupportsRayTracingPath() ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::IsRayTracingActiveCallback() {

		const SystemContext* context = GetCurrentContext();
		return context && context->graphicsPlatform &&
			context->graphicsPlatform->ShouldUseDispatchRays() ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::ResolveRenderFeaturePassCallback(
		const char* passName, uint64_t* outPassID,
		uint64_t* outGeneration) {

		if (!passName || passName[0] == '\0' || !outPassID ||
			!outGeneration) {

			return 0;
		}
		RenderFeatureProfileService& service =
			RenderFeatureProfileService::GetInstance();
		service.EnsureLoaded();
		const RenderFeaturePassSettings* pass =
			service.FindPassByName(passName);
		if (!pass) {
			return 0;
		}
		*outPassID = pass->id.value;
		*outGeneration = service.GetRuntimeGeneration();
		return 1;
	}

	int32_t ManagedScriptRuntime::ValidateRenderFeaturePassCallback(
		uint64_t passID, uint64_t generation) {

		return ResolveRenderFeaturePass(passID, generation) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::SetRenderFeaturePassEnabledCallback(
		uint64_t passID, uint64_t generation, int32_t enabled) {

		return ResolveRenderFeaturePass(passID, generation) &&
			RenderFeatureRuntimeOverrides::GetInstance().SetEnabled(
				UUID{ passID }, enabled != 0) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::SetRenderFeaturePassSceneColorOutputCallback(
		uint64_t passID, uint64_t generation, int32_t enabled) {

		if (!ResolveRenderFeaturePass(passID, generation)) {

			Logger::Output(LogType::Engine, spdlog::level::err,
				"[レンダー機能] SceneColor出力のパスハンドルが無効です");
			return 0;
		}
		return RenderFeatureRuntimeOverrides::GetInstance().SetSceneColorOutput(
			RenderFeatureProfileService::GetInstance().GetRuntime().GetProfile(),
			UUID{ passID }, enabled != 0) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::SetRenderFeatureGroupEnabledCallback(
		const char* groupName, int32_t enabled) {

		return groupName && RenderFeatureRuntimeOverrides::GetInstance().
			SetGroupEnabled(groupName, enabled != 0) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::SetRenderFeaturePassParameterCallback(
		uint64_t passID, uint64_t generation, uint64_t parameterID,
		const char* parameterName,
		const ManagedMaterialParameterValue* value) {

		if (!ResolveRenderFeaturePass(passID, generation) ||
			!parameterName || !value || parameterID == 0) {

			return 0;
		}
		MaterialParameterValue decoded{};
		if (!DecodeMaterialParameterValue(*value, decoded)) {
			return 0;
		}
		return RenderFeatureRuntimeOverrides::GetInstance().SetParameter(
			UUID{ passID }, MaterialParameterID{ parameterID },
			parameterName, decoded) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::GetRenderFeaturePassParameterCallback(
		uint64_t passID, uint64_t generation, uint64_t parameterID,
		ManagedMaterialParameterValue* outValue) {

		if (!outValue || parameterID == 0 ||
			!ResolveRenderFeaturePass(passID, generation)) {

			return 0;
		}
		const RenderFeaturePassRuntimeOverride* pass =
			RenderFeatureRuntimeOverrides::GetInstance().Find(UUID{ passID });
		const MaterialParameterValue* value = pass ?
			pass->parameters.Find(MaterialParameterID{ parameterID }) : nullptr;
		return value && EncodeMaterialParameterValue(*value, *outValue) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::ClearRenderFeaturePassParameterCallback(
		uint64_t passID, uint64_t generation, uint64_t parameterID) {

		return ResolveRenderFeaturePass(passID, generation) &&
			parameterID != 0 &&
			RenderFeatureRuntimeOverrides::GetInstance().ClearParameter(
				UUID{ passID }, MaterialParameterID{ parameterID }) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::ResetRenderFeaturePassCallback(
		uint64_t passID, uint64_t generation) {

		return ResolveRenderFeaturePass(passID, generation) &&
			RenderFeatureRuntimeOverrides::GetInstance().ResetPass(
				UUID{ passID }) ? 1 : 0;
	}

	void ManagedScriptRuntime::ResetRenderFeatureOverridesCallback() {

		RenderFeatureRuntimeOverrides::GetInstance().ResetAll();
	}

	int32_t ManagedScriptRuntime::CollisionGetShapePropertyCallback(ManagedNativeEntity entity,
		int32_t propertyId, void* out, int32_t size) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return 0;
		}
		const CollisionShape* shape = ResolveCollisionShape(*world, ResolveEntity(entity));
		if (!shape || !out) {
			return 0;
		}

		// propertyIdはC#側のCollisionShapeRefと対応する
		switch (propertyId) {
		case 0: if (size < 4) { return 0; } *reinterpret_cast<int32_t*>(out) = static_cast<int32_t>(shape->type); return 1;
		case 1: if (size < 4) { return 0; } *reinterpret_cast<int32_t*>(out) = shape->enabled ? 1 : 0; return 1;
		case 2: if (size < 4) { return 0; } *reinterpret_cast<int32_t*>(out) = shape->isTrigger ? 1 : 0; return 1;
		case 3: if (size < 4) { return 0; } *reinterpret_cast<int32_t*>(out) = shape->useTransformRotation ? 1 : 0; return 1;
		case 4: if (size < 4) { return 0; } *reinterpret_cast<int32_t*>(out) = shape->rotatedQuad ? 1 : 0; return 1;
		case 5: if (size < 12) { return 0; } std::memcpy(out, &shape->offset, 12); return 1;
		case 6: if (size < 12) { return 0; } std::memcpy(out, &shape->rotationDegrees, 12); return 1;
		case 7: if (size < 4) { return 0; } std::memcpy(out, &shape->radius, 4); return 1;
		case 8: if (size < 8) { return 0; } std::memcpy(out, &shape->halfSize2D, 8); return 1;
		case 9: if (size < 12) { return 0; } std::memcpy(out, &shape->halfExtents3D, 12); return 1;
		case 10: if (size < 4) { return 0; } std::memcpy(out, &shape->capsuleHeight, 4); return 1;
		case 11: if (size < 8) { return 0; } std::memcpy(out, &shape->capsuleSize2D, 8); return 1;
		case 12:
			if (size < 4) { return 0; }
			*reinterpret_cast<int32_t*>(out) = static_cast<int32_t>(shape->capsuleAxis);
			return 1;
		}
		return 0;
	}

	int32_t ManagedScriptRuntime::CollisionSetShapePropertyCallback(ManagedNativeEntity entity,
		int32_t propertyId, const void* value, int32_t size) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return 0;
		}
		const Entity resolved = ResolveEntity(entity);
		CollisionShape* shape = ResolveCollisionShape(*world, resolved);
		if (!shape || !value) {
			return 0;
		}

		switch (propertyId) {
		case 0:
			if (size < 4) { return 0; }
			shape->type = static_cast<ColliderShapeType>(
				*reinterpret_cast<const int32_t*>(value));
			break;
		case 1: if (size < 4) { return 0; } shape->enabled = *reinterpret_cast<const int32_t*>(value) != 0; break;
		case 2: if (size < 4) { return 0; } shape->isTrigger = *reinterpret_cast<const int32_t*>(value) != 0; break;
		case 3:
			if (size < 4) { return 0; }
			shape->useTransformRotation =
				*reinterpret_cast<const int32_t*>(value) != 0;
			break;
		case 4: if (size < 4) { return 0; } shape->rotatedQuad = *reinterpret_cast<const int32_t*>(value) != 0; break;
		case 5: if (size < 12) { return 0; } std::memcpy(&shape->offset, value, 12); break;
		case 6: if (size < 12) { return 0; } std::memcpy(&shape->rotationDegrees, value, 12); break;
		case 7: if (size < 4) { return 0; } std::memcpy(&shape->radius, value, 4); break;
		case 8: if (size < 8) { return 0; } std::memcpy(&shape->halfSize2D, value, 8); break;
		case 9: if (size < 12) { return 0; } std::memcpy(&shape->halfExtents3D, value, 12); break;
		case 10: if (size < 4) { return 0; } std::memcpy(&shape->capsuleHeight, value, 4); break;
		case 11: if (size < 8) { return 0; } std::memcpy(&shape->capsuleSize2D, value, 8); break;
		case 12:
			if (size < 4) { return 0; }
			shape->capsuleAxis = static_cast<CapsuleAxis>(
				*reinterpret_cast<const int32_t*>(value));
			break;
		default:
			return 0;
		}
		world->MarkComponentModified<CollisionComponent>(resolved);
		return 1;
	}

	float ManagedScriptRuntime::GetSkinnedAnimationDurationCallback(ManagedNativeEntity entity, const char* clipName) {

		if (!clipName) {
			return 0.0f;
		}
		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = ResolveWorld(entity);
		if (!context || !context->skinnedAnimationManager || !world) {
			return 0.0f;
		}
		const Entity resolved = ResolveEntity(entity);
		const MeshRendererComponent* renderer = world->IsAlive(resolved) ?
			world->TryGetComponent<MeshRendererComponent>(resolved) : nullptr;
		if (!renderer || !renderer->mesh) {
			return 0.0f;
		}
		// メッシュのアニメーションセットから指定クリップの合計長を引く
		const SkinnedMeshAnimationSet* animationSet = context->skinnedAnimationManager->Find(renderer->mesh);
		if (!animationSet || !animationSet->valid) {
			return 0.0f;
		}
		const auto it = animationSet->clips.find(clipName);
		return it != animationSet->clips.end() ? it->second.duration : 0.0f;
	}

	void ManagedScriptRuntime::PlaySkinnedAnimationCallback(ManagedNativeEntity entity, const char* clipName) {

		if (!clipName) {
			return;
		}
		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return;
		}
		const Entity resolved = ResolveEntity(entity);
		SkinnedAnimationComponent* anim = world->IsAlive(resolved) ?
			world->TryGetComponent<SkinnedAnimationComponent>(resolved) : nullptr;
		if (!anim) {
			return;
		}
		// 指定クリップへ切り替えて再生する、終了フラグを同フレームで下ろす
		// 実際の遷移や再生時間のリセットはSkinnedAnimationSystemが行う
		anim->clip = clipName;
		anim->enabled = true;
		if (SkinnedAnimationRuntimeData* runtime =
			TryGetSkinnedAnimationRuntime(*world, resolved)) {
			runtime->animationFinished = false;
		}
	}

	int32_t ManagedScriptRuntime::CopySkinnedAnimationCurrentClipCallback(
		ManagedNativeEntity entity, char* buffer, int32_t capacity) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		const SkinnedAnimationRuntimeData* runtime =
			world && world->IsAlive(resolved) ?
			TryGetSkinnedAnimationRuntime(*world, resolved) : nullptr;
		return CopyStringToBuffer(
			runtime ? runtime->currentClip : std::string{}, buffer, capacity);
	}

	int32_t ManagedScriptRuntime::GetSkinnedAnimationRuntimeStateCallback(
		ManagedNativeEntity entity,
		ManagedSkinnedAnimationRuntimeState* outState) {

		if (!outState) {
			return 0;
		}
		*outState = {};

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		const SkinnedAnimationRuntimeData* runtime =
			world && world->IsAlive(resolved) ?
			TryGetSkinnedAnimationRuntime(*world, resolved) : nullptr;
		if (!runtime) {
			return 0;
		}

		// 可変長データを跨がせずC#が必要な固定長状態だけを複写する
		outState->currentTime = runtime->time;
		outState->currentDuration = runtime->currentDuration;
		outState->blendTime = runtime->blendTime;
		outState->repeatCount = runtime->repeatCount;
		outState->initialized = runtime->initialized ? 1 : 0;
		outState->finished = runtime->animationFinished ? 1 : 0;
		outState->inTransition = runtime->inTransition ? 1 : 0;
		return 1;
	}

	int32_t ManagedScriptRuntime::LineAddPointCallback(ManagedNativeEntity entity, ManagedLinePoint point) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return -1;
		}
		const Entity resolved = ResolveEntity(entity);
		LineRendererComponent* line = world->IsAlive(resolved) ?
			world->TryGetComponent<LineRendererComponent>(resolved) : nullptr;
		if (!line) {
			return -1;
		}
		DynamicBuffer<LinePoint> points = world->TryGetBuffer<LinePoint>(resolved);
		if (!points.IsValid()) {
			points = world->AddBuffer<LinePoint>(resolved);
		}
		points.Add(ToLinePoint(point));
		world->MarkComponentModified<LinePoint>(resolved);
		// 追加した点の位置をC#へ返す、UpdatePointの対象指定に使う
		return static_cast<int32_t>(points.GetSize() - 1);
	}

	void ManagedScriptRuntime::LineUpdatePointCallback(ManagedNativeEntity entity, ManagedLinePoint point) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return;
		}
		const Entity resolved = ResolveEntity(entity);
		LineRendererComponent* line = world->IsAlive(resolved) ?
			world->TryGetComponent<LineRendererComponent>(resolved) : nullptr;
		if (!line) {
			return;
		}
		DynamicBuffer<LinePoint> points = world->TryGetBuffer<LinePoint>(resolved);
		// ClearやSetPoints後に残った古いindexを弾く
		if (!points.IsValid() || point.index < 0 ||
			points.GetSize() <= static_cast<uint32_t>(point.index)) {
			return;
		}
		points[static_cast<uint32_t>(point.index)] = ToLinePoint(point);
		world->MarkComponentModified<LinePoint>(resolved);
	}

	void ManagedScriptRuntime::LineDrawImmediateCallback(const ManagedLinePoint* points,
		int32_t count, int32_t loop, int32_t is2D, ManagedAssetGUID materialID) {

		if (points == nullptr || count < 2) {
			return;
		}

		// 即時バッファへ積むため一旦エンジン型へ変換する
		std::vector<LinePoint> converted;
		converted.reserve(static_cast<size_t>(count));
		for (int32_t i = 0; i < count; ++i) {
			converted.emplace_back(ToLinePoint(points[i]));
		}
		LineImmediateBuffer::GetInstance().AddPolyline(converted.data(), static_cast<uint32_t>(count),
			true, loop != 0, is2D != 0, ToAssetID(materialID));
	}

	void ManagedScriptRuntime::LineDrawSphereImmediateCallback(ManagedVector3 center, float radius,
		ManagedColor4 color, int32_t division, float thickness, ManagedAssetGUID materialID) {

		const uint32_t safeDivision = division < 3 ? 3u : static_cast<uint32_t>(division);
		LineImmediateBuffer::GetInstance().AddSphere(
			Vector3(center.x, center.y, center.z),
			radius, Color4(color.r, color.g, color.b, color.a),
			safeDivision, thickness, ToAssetID(materialID));
	}

	void ManagedScriptRuntime::LineDrawShapeCallback(const ManagedLineShape* shape) {

		if (shape == nullptr) {
			return;
		}

		const Color4 color(shape->color.r, shape->color.g, shape->color.b, shape->color.a);
		const Vector3 a(shape->a.x, shape->a.y, shape->a.z);
		const Vector3 b(shape->b.x, shape->b.y, shape->b.z);
		const Quaternion rotation(shape->rotation.x, shape->rotation.y, shape->rotation.z, shape->rotation.w);
		const uint32_t division = shape->division < 3 ? 3u : static_cast<uint32_t>(shape->division);

		// 形状種別ごとに線分リストへ展開する
		std::vector<LinePoint> segments;
		switch (static_cast<ManagedLineShapeKind>(shape->shapeType)) {
		case ManagedLineShapeKind::Circle2D:
			LineShapeBuilder::BuildCircle2D(Vector2(a.x, a.y), shape->radius, color, division, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::Rect2D:
			LineShapeBuilder::BuildRect2D(Vector2(a.x, a.y), Vector2(b.x, b.y), rotation, color, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::Hemisphere:
			LineShapeBuilder::BuildHemisphere(a, shape->radius, rotation, color, division, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::AABB:
			LineShapeBuilder::BuildAABB(a, b, color, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::OBB:
			LineShapeBuilder::BuildOBB(a, b, rotation, color, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::Cone:
			LineShapeBuilder::BuildCone(a, shape->radius, shape->radius2, shape->height, rotation, color, division, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::Arrow:
			LineShapeBuilder::BuildArrow(a, shape->height, rotation, color, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::Axis:
			LineShapeBuilder::BuildAxis(a, rotation, shape->height, shape->thickness, segments);
			break;
		default:
			return;
		}

		if (segments.size() < 2) {
			return;
		}
		// 形状は2点ずつ独立した線分リストなのでconnected=falseで積む
		LineImmediateBuffer::GetInstance().AddPolyline(segments.data(), static_cast<uint32_t>(segments.size()),
			false, false, shape->is2D != 0, ToAssetID(shape->materialID));
	}

	ManagedNativeEntity ManagedScriptRuntime::CreateEntityCallback(const char* name, ManagedNativeEntity parent) {

		ECSWorld* world = ResolveTargetWorld(parent);
		if (!world) {
			return MakeNullNativeEntity();
		}
		// 空Entityを即時予約する、emptyArchetypeへの行追加のみでコンポーネント追加つまりarchetype移行はflushへ
		const Entity reserved = world->CreateEntity();
		const Entity parentEntity = world->IsAlive(ResolveEntity(parent)) ? ResolveEntity(parent) : Entity::Null();
		world->GetCommandBuffer().EnqueueCreateEntity(reserved, name ? name : "", parentEntity);
		return MakeNativeEntity(*world, reserved);
	}

	ManagedNativeEntity ManagedScriptRuntime::InstantiatePrefabCallback(ManagedAssetGUID prefabAssetID,
		ManagedVector3 position, ManagedQuaternion rotation, int32_t useTransform, ManagedNativeEntity parent) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = ResolveTargetWorld(parent);
		const AssetID prefabAsset = ToAssetID(prefabAssetID);
		if (!context || !world || !prefabAsset) {
			return MakeNullNativeEntity();
		}
		const WorldCommandServices& services = world->GetCommandServices();
		if (!services.assetDatabase) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"Prefab.Instantiate: AssetDatabaseが未設定のためPrefabを生成できません");
			return MakeNullNativeEntity();
		}

		const Entity parentEntity = world->IsAlive(ResolveEntity(parent)) ? ResolveEntity(parent) : Entity::Null();
		PrefabInstantiateDesc desc{};
		desc.parent = parentEntity;
		if (world->IsAlive(parentEntity)) {
			if (const SceneObjectComponent* sceneObject = world->TryGetComponent<SceneObjectComponent>(parentEntity)) {
				desc.ownerSceneInstanceID = sceneObject->sceneInstanceID;
			}
		}
		if (!desc.ownerSceneInstanceID && services.sceneInstances) {
			if (const SceneInstance* activeScene = services.sceneInstances->GetActive()) {
				desc.ownerSceneInstanceID = activeScene->instanceID;
			}
		}

		HierarchySystem hierarchySystem{};
		PrefabSystem prefabSystem{};
		PrefabInstantiateResult result{};
		if (!prefabSystem.InstantiatePrefab(*services.assetDatabase, hierarchySystem, *world,
			prefabAsset, result, desc)) {

			for (auto it = result.createdEntities.rbegin(); it != result.createdEntities.rend(); ++it) {
				if (world->IsAlive(*it)) {
					world->DestroyEntity(*it);
				}
			}
			Logger::Output(LogType::Engine, spdlog::level::err,
				"Prefab.Instantiate: Prefabが存在しないかデータが不正です AssetID={}", ToString(prefabAsset));
			return MakeNullNativeEntity();
		}
		if (useTransform != 0) {
			if (TransformComponent* transform = world->TryGetComponent<TransformComponent>(result.root)) {
				transform->localPos = Vector3(position.x, position.y, position.z);
				transform->localRotation = Quaternion::Normalize(
					Quaternion(rotation.x, rotation.y, rotation.z, rotation.w));
			}
		}

		// 呼び出し元はSystemSchedulerが所有する可変ContextのためLifecycle同期へ戻す
		BehaviorSystem::SynchronizeInstantiatedEntities(
			*world, *const_cast<SystemContext*>(context), result.createdEntities);
		return MakeNativeEntity(*world, result.root);
	}

	int32_t ManagedScriptRuntime::DontDestroyOnLoadCallback(ManagedNativeEntity entity) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = ResolveWorld(entity);
		SceneInstanceManager* scenes = world ? world->GetCommandServices().sceneInstances : nullptr;
		if (!context || context->mode != WorldMode::Play || !scenes ||
			!scenes->DontDestroyOnLoad(*world, ResolveEntity(entity))) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"DontDestroyOnLoad: Play中の有効なルートEntityを指定してください");
			return 0;
		}
		return 1;
	}

	uint64_t ManagedScriptRuntime::LoadSceneAdditiveCallback(ManagedAssetGUID sceneAssetID) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		const AssetID sceneAsset = ToAssetID(sceneAssetID);
		if (!world || !sceneAsset) {
			return 0;
		}
		// instance IDを先行採番してC#のSceneHandleと一致させ、load自体はflushへ回す
		const UUID instanceID = UUID::New();
		world->GetCommandBuffer().EnqueueLoadSceneAdditive(instanceID, sceneAsset);
		return instanceID.value;
	}

	uint64_t ManagedScriptRuntime::LoadSceneSingleCallback(ManagedAssetGUID sceneAssetID) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		const AssetID sceneAsset = ToAssetID(sceneAssetID);
		if (!world) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"SceneManager.LoadScene: 実行中のWorldを取得できないため単一Sceneロードを拒否しました");
			return 0;
		}
		if (!sceneAsset) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"SceneManager.LoadScene: SceneAssetが無効なため単一Sceneロードを拒否しました");
			return 0;
		}
		SceneInstanceManager* sceneInstances =
			world->GetCommandServices().sceneInstances;
		if (!sceneInstances) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"SceneManager.LoadScene: SceneInstanceManagerが未設定のため単一Sceneロードを拒否しました");
			return 0;
		}
		if (!sceneInstances->TryBeginSingleLoadRequest()) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"SceneManager.LoadScene: 単一Sceneロード要求を処理中のため新しい要求を拒否しました");
			return 0;
		}
		// 単一ロード、新sceneをactiveにし旧sceneを全てアンロードする処理はflushで行う
		const UUID instanceID = UUID::New();
		world->GetCommandBuffer().EnqueueLoadSceneSingle(instanceID, sceneAsset);
		return instanceID.value;
	}

	uint64_t ManagedScriptRuntime::ReloadActiveSceneCallback() {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"SceneManager.ReloadActiveScene: 実行中のWorldを取得できないため再読み込みを拒否しました");
			return 0;
		}
		SceneInstanceManager* sceneInstances =
			world->GetCommandServices().sceneInstances;
		if (!sceneInstances) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"SceneManager.ReloadActiveScene: SceneInstanceManagerが未設定のため再読み込みを拒否しました");
			return 0;
		}
		const SceneInstance* activeScene = sceneInstances->GetActive();
		const AssetID sceneAsset = activeScene ? activeScene->sceneAsset : AssetID{};
		if (!sceneAsset) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"SceneManager.ReloadActiveScene: アクティブSceneを取得できないため再読み込みを拒否しました");
			return 0;
		}
		if (!sceneInstances->TryBeginSingleLoadRequest()) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"SceneManager.ReloadActiveScene: 単一Sceneロード要求を処理中のため再読み込みを拒否しました");
			return 0;
		}

		const UUID instanceID = UUID::New();
		world->GetCommandBuffer().EnqueueLoadSceneSingle(instanceID, sceneAsset);
		return instanceID.value;
	}

	void ManagedScriptRuntime::UnloadSceneCallback(uint64_t sceneInstanceID) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world || sceneInstanceID == 0) {
			return;
		}
		world->GetCommandBuffer().EnqueueUnloadScene(UUID{ sceneInstanceID });
	}

	int32_t ManagedScriptRuntime::IsSceneInstanceAliveCallback(uint64_t sceneInstanceID) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world || sceneInstanceID == 0) {
			return 0;
		}
		const WorldCommandServices& services = world->GetCommandServices();
		if (!services.sceneInstances) {
			return 0;
		}
		return services.sceneInstances->Find(UUID{ sceneInstanceID }) != nullptr ? 1 : 0;
	}

	void ManagedScriptRuntime::SetParentKeepWorldCallback(ManagedNativeEntity child, ManagedNativeEntity parent, int32_t worldPositionStays) {

		EnqueueSetParentCommand(child, parent, worldPositionStays != 0);
	}

	//============================================================================
	//	AudioSourceのゲームプレイメソッド
	//	実際の音声制御はAudioSourceSystemが再生要求を順番に消費する
	//============================================================================
	namespace {

		// 対象AudioSourceと所有Worldを同時に解決する
		bool ResolveAudioSource(
			ManagedNativeEntity entity, ECSWorld*& outWorld,
			Entity& outEntity) {

			outWorld = ResolveWorld(entity);
			outEntity = ResolveEntity(entity);
			return outWorld && outWorld->IsAlive(outEntity) &&
				outWorld->HasComponent<AudioSourceComponent>(outEntity);
		}
	}

	void ManagedScriptRuntime::AudioPlayCallback(ManagedNativeEntity entity) {
		ECSWorld* world = nullptr;
		Entity resolved = Entity::Null();
		if (ResolveAudioSource(entity, world, resolved)) {
			RequestAudioPlay(*world, resolved);
		}
	}

	void ManagedScriptRuntime::AudioPlayOneShotCallback(
		ManagedNativeEntity entity, ManagedAssetGUID clipID, float volumeScale) {

		ECSWorld* world = nullptr;
		Entity resolved = Entity::Null();
		if (ResolveAudioSource(entity, world, resolved)) {
			RequestAudioPlayOneShot(
				*world, resolved, ToAssetID(clipID), volumeScale);
		}
	}

	void ManagedScriptRuntime::AudioPauseCallback(ManagedNativeEntity entity) {
		ECSWorld* world = nullptr;
		Entity resolved = Entity::Null();
		if (ResolveAudioSource(entity, world, resolved)) {
			RequestAudioPause(*world, resolved);
		}
	}

	void ManagedScriptRuntime::AudioUnPauseCallback(ManagedNativeEntity entity) {
		ECSWorld* world = nullptr;
		Entity resolved = Entity::Null();
		if (ResolveAudioSource(entity, world, resolved)) {
			RequestAudioUnPause(*world, resolved);
		}
	}

	void ManagedScriptRuntime::AudioStopCallback(ManagedNativeEntity entity) {
		ECSWorld* world = nullptr;
		Entity resolved = Entity::Null();
		if (ResolveAudioSource(entity, world, resolved)) {
			RequestAudioStop(*world, resolved);
		}
	}

	int32_t ManagedScriptRuntime::AudioIsPlayingCallback(ManagedNativeEntity entity) {
		ECSWorld* world = nullptr;
		Entity resolved = Entity::Null();
		return ResolveAudioSource(entity, world, resolved) &&
			IsAudioSourcePlaying(*world, resolved) ? 1 : 0;
	}

} // Engine
