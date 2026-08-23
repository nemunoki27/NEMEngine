#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Audio/AudioSourceComponent.h>
#include <Engine/Core/World/Components/Rendering/LineRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/EffectEmitterComponent.h>
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
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineImmediateBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineShapeBuilder.h>
#include <Engine/Core/Assets/AssetTypes.h>
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
	//	Entity生成とPrefabとSceneとSetParent、構造変更はWorldCommandBuffer経由で遅延適用する
	//	生成系は空Entityを即時予約してハンドルを返しコンポーネントと名前とparentはflushで適用する
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

	namespace {

		enum class EffectStateProperty :
			int32_t {

			Enabled,
			Effect,
			Mode,
			Delay,
			Count,
			Interval,
			Duration,
			EmitUntilStopped,
			LocalPosition,
			LocalRotation,
			LocalScale,
			UseEmitterParent,
			ParentEntity,
			IgnoreParentRotation,
			IgnoreParentScale,
			KeepWorldOnDetach,
		};

		// 対象entityのEffectEmitterComponentを取得する
		EffectEmitterComponent* ResolveEffectEmitter(ManagedNativeEntity entity) {

			ECSWorld* world = ResolveWorld(entity);
			if (!world) { return nullptr; }
			const Entity resolved = ResolveEntity(entity);
			return world->IsAlive(resolved) ? world->TryGetComponent<EffectEmitterComponent>(resolved) : nullptr;
		}

		// groupIndexとstateIndexから設定を取得する
		EffectEmitterState* ResolveEffectEmitterState(ManagedNativeEntity entity,
			int32_t groupIndex, int32_t stateIndex) {

			EffectEmitterComponent* emitter = ResolveEffectEmitter(entity);
			if (!emitter || groupIndex < 0 || stateIndex < 0 ||
				static_cast<size_t>(groupIndex) >= emitter->groups.size()) {
				return nullptr;
			}
			EffectEmitterGroup& group = emitter->groups[static_cast<size_t>(groupIndex)];
			return static_cast<size_t>(stateIndex) < group.states.size() ?
				&group.states[static_cast<size_t>(stateIndex)] : nullptr;
		}

		// POD値をManaged側のバッファへコピーする
		template <typename T>
		int32_t CopyEffectStateValue(const T& value, void* outData, int32_t capacity) {

			constexpr int32_t size = static_cast<int32_t>(sizeof(T));
			if (outData && size <= capacity) {
				std::memcpy(outData, &value, sizeof(T));
			}
			return size;
		}

		// Managed側のバッファからPOD値を読み取る
		template <typename T>
		bool ReadEffectStateValue(const void* data, int32_t size, T& outValue) {

			if (!data || size != static_cast<int32_t>(sizeof(T))) {
				return false;
			}
			std::memcpy(&outValue, data, sizeof(T));
			return true;
		}
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

	uint64_t ManagedScriptRuntime::EffectEmitCallback(ManagedNativeEntity entity, const char* group,
		ManagedVector3 position, ManagedQuaternion rotation, int32_t fixedAnchor) {

		EffectEmitterComponent* emitter = ResolveEffectEmitter(entity);
		if (!emitter) { return 0; }
		const std::string_view groupName = group ? std::string_view(group) : std::string_view{};
		if (fixedAnchor == 0) {
			return emitter->Emit(groupName);
		}
		return emitter->EmitAt(groupName, Vector3(position.x, position.y, position.z),
			Quaternion(rotation.x, rotation.y, rotation.z, rotation.w));
	}

	void ManagedScriptRuntime::EffectStopCallback(ManagedNativeEntity entity,
		uint64_t playbackID, const char* group, int32_t target) {

		EffectEmitterComponent* emitter = ResolveEffectEmitter(entity);
		if (!emitter) { return; }
		if (target == 0) {
			emitter->Stop(playbackID);
		} else if (target == 1) {
			emitter->Stop(group ? std::string_view(group) : std::string_view{});
		} else {
			emitter->Stop();
		}
	}

	void ManagedScriptRuntime::EffectClearCallback(ManagedNativeEntity entity,
		uint64_t playbackID, const char* group, int32_t target) {

		EffectEmitterComponent* emitter = ResolveEffectEmitter(entity);
		if (!emitter) { return; }
		if (target == 0) {
			emitter->Clear(playbackID);
		} else if (target == 1) {
			emitter->Clear(group ? std::string_view(group) : std::string_view{});
		} else {
			emitter->Clear();
		}
	}

	int32_t ManagedScriptRuntime::EffectIsPlayingCallback(ManagedNativeEntity entity,
		uint64_t playbackID, const char* group, int32_t target) {

		const EffectEmitterComponent* emitter = ResolveEffectEmitter(entity);
		if (!emitter) { return 0; }
		if (target == 0) {
			return emitter->IsPlaying(playbackID) ? 1 : 0;
		}
		if (target == 1) {
			return emitter->IsPlaying(group ? std::string_view(group) : std::string_view{}) ? 1 : 0;
		}
		if (!emitter->runtimePlaybacks.empty()) { return 1; }
		return std::any_of(emitter->runtimeCommands.begin(), emitter->runtimeCommands.end(),
			[](const EffectEmitterCommand& command) {
				return command.type == EffectEmitterCommandType::Emit;
			}) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::EffectGroupCountCallback(ManagedNativeEntity entity) {

		const EffectEmitterComponent* emitter = ResolveEffectEmitter(entity);
		return emitter ? static_cast<int32_t>(emitter->groups.size()) : 0;
	}

	int32_t ManagedScriptRuntime::EffectStateCountCallback(
		ManagedNativeEntity entity, int32_t groupIndex) {

		const EffectEmitterComponent* emitter = ResolveEffectEmitter(entity);
		if (!emitter || groupIndex < 0 || static_cast<size_t>(groupIndex) >= emitter->groups.size()) {
			return 0;
		}
		return static_cast<int32_t>(emitter->groups[static_cast<size_t>(groupIndex)].states.size());
	}

	int32_t ManagedScriptRuntime::EffectCopyGroupNameCallback(ManagedNativeEntity entity,
		int32_t groupIndex, char* buffer, int32_t capacity) {

		const EffectEmitterComponent* emitter = ResolveEffectEmitter(entity);
		if (!emitter || groupIndex < 0 || static_cast<size_t>(groupIndex) >= emitter->groups.size()) {
			return CopyStringToBuffer({}, buffer, capacity);
		}
		return CopyStringToBuffer(emitter->groups[static_cast<size_t>(groupIndex)].name, buffer, capacity);
	}

	int32_t ManagedScriptRuntime::EffectCopyStateNameCallback(ManagedNativeEntity entity,
		int32_t groupIndex, int32_t stateIndex, char* buffer, int32_t capacity) {

		const EffectEmitterState* state = ResolveEffectEmitterState(entity, groupIndex, stateIndex);
		return CopyStringToBuffer(state ? state->name : std::string{}, buffer, capacity);
	}

	int32_t ManagedScriptRuntime::EffectSetStateNameCallback(ManagedNativeEntity entity,
		int32_t groupIndex, int32_t stateIndex, const char* name) {

		EffectEmitterState* state = ResolveEffectEmitterState(entity, groupIndex, stateIndex);
		if (!state) { return 0; }
		state->name = name ? name : "";
		return 1;
	}

	int32_t ManagedScriptRuntime::EffectGetStatePropertyCallback(ManagedNativeEntity entity,
		int32_t groupIndex, int32_t stateIndex, int32_t property,
		void* outData, int32_t capacity) {

		const EffectEmitterState* state = ResolveEffectEmitterState(entity, groupIndex, stateIndex);
		if (!state) { return 0; }
		switch (static_cast<EffectStateProperty>(property)) {
		case EffectStateProperty::Enabled:
			return CopyEffectStateValue(state->enabled ? 1 : 0, outData, capacity);
		case EffectStateProperty::Effect:
			return CopyEffectStateValue(ToManagedAssetGUID(state->effect), outData, capacity);
		case EffectStateProperty::Mode:
			return CopyEffectStateValue(static_cast<int32_t>(state->mode), outData, capacity);
		case EffectStateProperty::Delay:
			return CopyEffectStateValue(state->delay, outData, capacity);
		case EffectStateProperty::Count:
			return CopyEffectStateValue(state->count, outData, capacity);
		case EffectStateProperty::Interval:
			return CopyEffectStateValue(state->interval, outData, capacity);
		case EffectStateProperty::Duration:
			return CopyEffectStateValue(state->duration, outData, capacity);
		case EffectStateProperty::EmitUntilStopped:
			return CopyEffectStateValue(state->emitUntilStopped ? 1 : 0, outData, capacity);
		case EffectStateProperty::LocalPosition:
			return CopyEffectStateValue(ToManagedVector3(state->localPosition), outData, capacity);
		case EffectStateProperty::LocalRotation:
			return CopyEffectStateValue(ToManagedQuaternion(state->localRotation), outData, capacity);
		case EffectStateProperty::LocalScale:
			return CopyEffectStateValue(ToManagedVector3(state->localScale), outData, capacity);
		case EffectStateProperty::UseEmitterParent:
			return CopyEffectStateValue(state->parentSettings.useEmitter ? 1 : 0, outData, capacity);
		case EffectStateProperty::ParentEntity: {

			ManagedNativeEntity parent{};
			ECSWorld* world = ResolveWorld(entity);
			if (world && state->parentSettings.entityLocalFileID) {
				const Entity resolved = SceneObjectUtility::FindByLocalFileID(
					*world, state->parentSettings.entityLocalFileID);
				if (world->IsAlive(resolved)) { parent = MakeNativeEntity(*world, resolved); }
			}
			return CopyEffectStateValue(parent, outData, capacity);
		}
		case EffectStateProperty::IgnoreParentRotation:
			return CopyEffectStateValue(state->parentSettings.ignoreParentRotation ? 1 : 0, outData, capacity);
		case EffectStateProperty::IgnoreParentScale:
			return CopyEffectStateValue(state->parentSettings.ignoreParentScale ? 1 : 0, outData, capacity);
		case EffectStateProperty::KeepWorldOnDetach:
			return CopyEffectStateValue(state->parentSettings.keepWorldOnDetach ? 1 : 0, outData, capacity);
		}
		return 0;
	}

	int32_t ManagedScriptRuntime::EffectSetStatePropertyCallback(ManagedNativeEntity entity,
		int32_t groupIndex, int32_t stateIndex, int32_t property,
		const void* data, int32_t size) {

		EffectEmitterState* state = ResolveEffectEmitterState(entity, groupIndex, stateIndex);
		if (!state) { return 0; }
		switch (static_cast<EffectStateProperty>(property)) {
		case EffectStateProperty::Enabled: {
			int32_t value = 0;
			if (!ReadEffectStateValue(data, size, value)) { return 0; }
			state->enabled = value != 0;
			return 1;
		}
		case EffectStateProperty::Effect: {
			ManagedAssetGUID value{};
			if (!ReadEffectStateValue(data, size, value)) { return 0; }
			state->effect = ToAssetID(value);
			return 1;
		}
		case EffectStateProperty::Mode: {
			int32_t value = 0;
			if (!ReadEffectStateValue(data, size, value) || value < 0 ||
				static_cast<int32_t>(EffectEmitterMode::Count) < value) {
				return 0;
			}
			state->mode = static_cast<EffectEmitterMode>(value);
			return 1;
		}
		case EffectStateProperty::Delay:
		case EffectStateProperty::Interval:
		case EffectStateProperty::Duration: {
			float value = 0.0f;
			if (!ReadEffectStateValue(data, size, value)) { return 0; }
			value = (std::max)(value, 0.0f);
			if (property == static_cast<int32_t>(EffectStateProperty::Delay)) { state->delay = value; }
			if (property == static_cast<int32_t>(EffectStateProperty::Interval)) { state->interval = value; }
			if (property == static_cast<int32_t>(EffectStateProperty::Duration)) { state->duration = value; }
			return 1;
		}
		case EffectStateProperty::Count: {
			int32_t value = 0;
			if (!ReadEffectStateValue(data, size, value)) { return 0; }
			state->count = (std::max)(value, 1);
			return 1;
		}
		case EffectStateProperty::EmitUntilStopped: {
			int32_t value = 0;
			if (!ReadEffectStateValue(data, size, value)) { return 0; }
			state->emitUntilStopped = value != 0;
			return 1;
		}
		case EffectStateProperty::LocalPosition:
		case EffectStateProperty::LocalScale: {
			ManagedVector3 value{};
			if (!ReadEffectStateValue(data, size, value)) { return 0; }
			const Vector3 native(value.x, value.y, value.z);
			if (property == static_cast<int32_t>(EffectStateProperty::LocalPosition)) {
				state->localPosition = native;
			} else {
				state->localScale = native;
			}
			return 1;
		}
		case EffectStateProperty::LocalRotation: {
			ManagedQuaternion value{};
			if (!ReadEffectStateValue(data, size, value)) { return 0; }
			const Quaternion rotation(value.x, value.y, value.z, value.w);
			state->localRotation = Quaternion::Length(rotation) <= 1.0e-6f ?
				Quaternion::Identity() : Quaternion::Normalize(rotation);
			return 1;
		}
		case EffectStateProperty::UseEmitterParent: {
			int32_t value = 0;
			if (!ReadEffectStateValue(data, size, value)) { return 0; }
			state->parentSettings.useEmitter = value != 0;
			if (state->parentSettings.useEmitter) { state->parentSettings.entityLocalFileID = {}; }
			return 1;
		}
		case EffectStateProperty::ParentEntity: {
			ManagedNativeEntity value{};
			if (!ReadEffectStateValue(data, size, value)) { return 0; }
			ECSWorld* world = ResolveWorld(entity);
			ECSWorld* parentWorld = ResolveWorld(value);
			if (!parentWorld) {

				state->parentSettings.entityLocalFileID = {};
				return 1;
			}
			const Entity parent = ResolveEntity(value);
			if (!world || parentWorld != world || !world->IsAlive(parent)) { return 0; }
			const SceneObjectComponent* sceneObject = world->TryGetComponent<SceneObjectComponent>(parent);
			if (!sceneObject || !sceneObject->localFileID) { return 0; }
			state->parentSettings.entityLocalFileID = sceneObject->localFileID;
			state->parentSettings.useEmitter = false;
			return 1;
		}
		case EffectStateProperty::IgnoreParentRotation:
		case EffectStateProperty::IgnoreParentScale:
		case EffectStateProperty::KeepWorldOnDetach: {
			int32_t value = 0;
			if (!ReadEffectStateValue(data, size, value)) { return 0; }
			const bool enabled = value != 0;
			if (property == static_cast<int32_t>(EffectStateProperty::IgnoreParentRotation)) {
				state->parentSettings.ignoreParentRotation = enabled;
			}
			if (property == static_cast<int32_t>(EffectStateProperty::IgnoreParentScale)) {
				state->parentSettings.ignoreParentScale = enabled;
			}
			if (property == static_cast<int32_t>(EffectStateProperty::KeepWorldOnDetach)) {
				state->parentSettings.keepWorldOnDetach = enabled;
			}
			return 1;
		}
		}
		return 0;
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

		// shapeIndexからCollisionComponentの衝突形状を取得する、範囲外はnullptr
		Engine::CollisionShape* ResolveCollisionShape(Engine::ECSWorld& world, const Engine::Entity& entity, int32_t shapeIndex) {

			if (!world.IsAlive(entity)) {
				return nullptr;
			}
			Engine::CollisionComponent* collision = world.TryGetComponent<Engine::CollisionComponent>(entity);
			if (!collision || shapeIndex < 0) {
				return nullptr;
			}
			return Engine::TryGetCollisionShape(
				world, entity, static_cast<uint32_t>(shapeIndex));
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
		const int32_t updated = VisitRendererMaterialInstances(
			*world, resolved,
			static_cast<ManagedRendererMaterialTarget>(target), subMeshIndex,
			[&](MaterialParameterSet& materialInstance) {
				materialInstance.Set(
					id, parameterName,
					ResolveMaterialParameterSemantic(parameterName), decoded);
			});
		if (updated != 0) {
			// 外部ストレージを含むRenderer変更を描画バッチへ通知する
			world->MarkRenderDataModified();
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

	int32_t ManagedScriptRuntime::SetRenderFeaturePassEnabledCallback(
		const char* passName, int32_t enabled) {

		return passName &&
			RenderFeatureRuntimeOverrides::GetInstance().SetEnabled(
				passName, enabled != 0) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::SetRenderFeaturePassParameterCallback(
		const char* passName, uint64_t parameterID,
		const char* parameterName,
		const ManagedMaterialParameterValue* value) {

		if (!passName || !parameterName || !value || parameterID == 0) {
			return 0;
		}
		MaterialParameterValue decoded{};
		if (!DecodeMaterialParameterValue(*value, decoded)) {
			return 0;
		}
		return RenderFeatureRuntimeOverrides::GetInstance().SetParameter(
			passName, MaterialParameterID{ parameterID },
			parameterName, decoded) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::ClearRenderFeaturePassParameterCallback(
		const char* passName, uint64_t parameterID) {

		return passName && parameterID != 0 &&
			RenderFeatureRuntimeOverrides::GetInstance().ClearParameter(
				passName, MaterialParameterID{ parameterID }) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::ResetRenderFeaturePassCallback(
		const char* passName) {

		return passName &&
			RenderFeatureRuntimeOverrides::GetInstance().ResetPass(passName) ? 1 : 0;
	}

	void ManagedScriptRuntime::ResetRenderFeatureOverridesCallback() {

		RenderFeatureRuntimeOverrides::GetInstance().ResetAll();
	}

	int32_t ManagedScriptRuntime::CollisionShapeCountCallback(ManagedNativeEntity entity) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return 0;
		}
		const Entity resolved = ResolveEntity(entity);
		const CollisionComponent* collision = world->IsAlive(resolved) ?
			world->TryGetComponent<CollisionComponent>(resolved) : nullptr;
		return collision ?
			static_cast<int32_t>(GetCollisionShapes(*world, resolved).size()) : 0;
	}

	void ManagedScriptRuntime::CollisionAddShapeCallback(ManagedNativeEntity entity) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return;
		}
		const Entity resolved = ResolveEntity(entity);
		CollisionComponent* collision = world->IsAlive(resolved) ?
			world->TryGetComponent<CollisionComponent>(resolved) : nullptr;
		if (collision) {
			AddCollisionShape(*world, resolved);
			world->MarkComponentModified<CollisionComponent>(resolved);
		}
	}

	void ManagedScriptRuntime::CollisionRemoveShapeAtCallback(ManagedNativeEntity entity, int32_t shapeIndex) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return;
		}
		const Entity resolved = ResolveEntity(entity);
		CollisionComponent* collision = world->IsAlive(resolved) ?
			world->TryGetComponent<CollisionComponent>(resolved) : nullptr;
		if (collision && 0 <= shapeIndex &&
			RemoveCollisionShape(*world, resolved, static_cast<uint32_t>(shapeIndex))) {

			world->MarkComponentModified<CollisionComponent>(resolved);
		}
	}

	void ManagedScriptRuntime::CollisionClearShapesCallback(ManagedNativeEntity entity) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return;
		}
		const Entity resolved = ResolveEntity(entity);
		CollisionComponent* collision = world->IsAlive(resolved) ?
			world->TryGetComponent<CollisionComponent>(resolved) : nullptr;
		if (collision) {
			ClearCollisionShapes(*world, resolved);
			world->MarkComponentModified<CollisionComponent>(resolved);
		}
	}

	int32_t ManagedScriptRuntime::CollisionGetShapePropertyCallback(ManagedNativeEntity entity,
		int32_t shapeIndex, int32_t propertyId, void* out, int32_t size) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return 0;
		}
		const CollisionShape* shape = ResolveCollisionShape(*world, ResolveEntity(entity), shapeIndex);
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
		int32_t shapeIndex, int32_t propertyId, const void* value, int32_t size) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return 0;
		}
		const Entity resolved = ResolveEntity(entity);
		CollisionShape* shape = ResolveCollisionShape(*world, resolved, shapeIndex);
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
		RebuildCollisionCompound(*world, resolved);
		world->MarkComponentModified<CollisionShape>(resolved);
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

		ECSWorld* world = ResolveTargetWorld(parent);
		const AssetID prefabAsset = ToAssetID(prefabAssetID);
		if (!world || !prefabAsset) {
			return MakeNullNativeEntity();
		}
		// ルートEntityを即時予約しPrefabSystemにはreservedRootを渡して実体化させる、遅延でも実rootを返す
		const Entity reservedRoot = world->CreateEntity();
		const Entity parentEntity = world->IsAlive(ResolveEntity(parent)) ? ResolveEntity(parent) : Entity::Null();
		world->GetCommandBuffer().EnqueueInstantiatePrefab(reservedRoot, prefabAsset,
			Vector3(position.x, position.y, position.z),
			Quaternion(rotation.x, rotation.y, rotation.z, rotation.w),
			useTransform != 0, parentEntity);
		return MakeNativeEntity(*world, reservedRoot);
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
		if (!world || !sceneAsset) {
			return 0;
		}
		SceneInstanceManager* sceneInstances =
			world->GetCommandServices().sceneInstances;
		if (!sceneInstances || !sceneInstances->TryBeginSingleLoadRequest()) {
			return 0;
		}
		// 単一ロード、新sceneをactiveにし旧sceneを全てアンロードする処理はflushで行う
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

		ECSWorld* world = ResolveWorld(child);
		if (!world) {
			return;
		}
		const Entity childEntity = ResolveEntity(child);
		const Entity parentEntity = ResolveEntity(parent);
		world->GetCommandBuffer().EnqueueSetParent(childEntity, parentEntity, worldPositionStays != 0);
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
