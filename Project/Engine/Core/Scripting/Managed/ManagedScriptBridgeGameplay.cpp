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
#include <Engine/Core/World/Components/Rendering/FillFaceMeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/EffectEmitterComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>
#include <Engine/Core/World/Components/UI/IrisTransitionComponent.h>
#include <Engine/Core/World/UI/UIRuntimeService.h>
#include <Engine/Core/Rendering/Meshes/Animation/SkinnedMeshAnimationManager.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineImmediateBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineShapeBuilder.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <algorithm>
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

		// Canvasのキーボード入力配列を取得する
		std::vector<KeyDIKCode>* ResolveCanvasKeyBindings(
			CanvasComponent& canvas, int32_t action) {

			switch (action) {
			case 0: return &canvas.navigationUpKeys;
			case 1: return &canvas.navigationDownKeys;
			case 2: return &canvas.navigationLeftKeys;
			case 3: return &canvas.navigationRightKeys;
			case 4: return &canvas.submitKeys;
			default: return nullptr;
			}
		}

		// Canvasのゲームパッド入力配列を取得する
		std::vector<GamePadButtons>* ResolveCanvasGamepadBindings(
			CanvasComponent& canvas, int32_t action) {

			switch (action) {
			case 0: return &canvas.navigationUpGamepadButtons;
			case 1: return &canvas.navigationDownGamepadButtons;
			case 2: return &canvas.navigationLeftGamepadButtons;
			case 3: return &canvas.navigationRightGamepadButtons;
			case 4: return &canvas.submitGamepadButtons;
			default: return nullptr;
			}
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

		// count0はクリア扱い、点列を丸ごと差し替える
		line->points.clear();
		if (points != nullptr && count > 0) {

			line->points.reserve(static_cast<size_t>(count));
			for (int32_t i = 0; i < count; ++i) {
				line->points.emplace_back(ToLinePoint(points[i]));
			}
		}
		line->loop = (loop != 0);
	}

	void ManagedScriptRuntime::FillMeshSetPositionsCallback(ManagedNativeEntity entity,
		const ManagedVector3* points, int32_t count) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return;
		}
		const Entity resolved = ResolveEntity(entity);
		FillMeshRendererComponent* fillMesh = world->IsAlive(resolved) ?
			world->TryGetComponent<FillMeshRendererComponent>(resolved) : nullptr;
		if (!fillMesh) {
			return;
		}

		// count0はクリア扱い、点列を丸ごと差し替える
		fillMesh->facePositions.clear();
		if (points != nullptr && count > 0) {

			fillMesh->facePositions.reserve(static_cast<size_t>(count));
			for (int32_t i = 0; i < count; ++i) {
				fillMesh->facePositions.emplace_back(points[i].x, points[i].y, points[i].z);
			}
		}
	}

	int32_t ManagedScriptRuntime::FillMeshCopyPositionsCallback(ManagedNativeEntity entity,
		ManagedVector3* points, int32_t capacity, int32_t worldSpace) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return 0;
		}
		const Entity resolved = ResolveEntity(entity);
		const FillMeshRendererComponent* fillMesh = world->IsAlive(resolved) ?
			world->TryGetComponent<FillMeshRendererComponent>(resolved) : nullptr;
		if (!fillMesh) {
			return 0;
		}

		const int32_t count = static_cast<int32_t>(fillMesh->facePositions.size());
		if (!points || capacity <= 0) {
			return count;
		}

		Matrix4x4 worldMatrix = Matrix4x4::Identity();
		if (worldSpace != 0) {
			if (const TransformComponent* transform = world->TryGetComponent<TransformComponent>(resolved)) {
				worldMatrix = transform->worldMatrix;
			}
		}

		const int32_t copyCount = (std::min)(count, capacity);
		for (int32_t i = 0; i < copyCount; ++i) {

			const Vector3 position = worldSpace != 0 ?
				Vector3::Transform(fillMesh->facePositions[static_cast<size_t>(i)], worldMatrix) :
				fillMesh->facePositions[static_cast<size_t>(i)];
			points[i] = { position.x, position.y, position.z };
		}
		return count;
	}

	int32_t ManagedScriptRuntime::CanvasCopyInputBindingsCallback(
		ManagedNativeEntity entity, int32_t action, int32_t device,
		int32_t* bindings, int32_t capacity) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return 0;
		}
		const Entity resolved = ResolveEntity(entity);
		CanvasComponent* canvas = world->IsAlive(resolved) ?
			world->TryGetComponent<CanvasComponent>(resolved) : nullptr;
		if (!canvas || device < 0 || 1 < device) {
			return 0;
		}

		const std::vector<KeyDIKCode>* keys =
			device == 0 ? ResolveCanvasKeyBindings(*canvas, action) : nullptr;
		const std::vector<GamePadButtons>* buttons =
			device == 1 ? ResolveCanvasGamepadBindings(*canvas, action) : nullptr;
		if (!keys && !buttons) {
			return 0;
		}
		const int32_t count = keys ?
			static_cast<int32_t>(keys->size()) : static_cast<int32_t>(buttons->size());
		if (!bindings || capacity <= 0) {
			return count;
		}

		const int32_t copyCount = (std::min)(count, capacity);
		for (int32_t i = 0; i < copyCount; ++i) {
			bindings[i] = keys ?
				static_cast<int32_t>((*keys)[static_cast<size_t>(i)]) :
				static_cast<int32_t>((*buttons)[static_cast<size_t>(i)]);
		}
		return count;
	}

	void ManagedScriptRuntime::CanvasSetInputBindingsCallback(
		ManagedNativeEntity entity, int32_t action, int32_t device,
		const int32_t* bindings, int32_t count) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world || device < 0 || 1 < device || count < 0) {
			return;
		}
		const Entity resolved = ResolveEntity(entity);
		CanvasComponent* canvas = world->IsAlive(resolved) ?
			world->TryGetComponent<CanvasComponent>(resolved) : nullptr;
		if (!canvas) {
			return;
		}

		if (device == 0) {
			std::vector<KeyDIKCode>* keys = ResolveCanvasKeyBindings(*canvas, action);
			if (!keys) {
				return;
			}
			keys->clear();
			for (int32_t i = 0; bindings && i < count; ++i) {

				const int32_t code = bindings[i];
				const KeyDIKCode key = static_cast<KeyDIKCode>(code);
				if (0 < code && code <= 255 &&
					std::find(keys->begin(), keys->end(), key) == keys->end()) {
					keys->emplace_back(key);
				}
			}
			return;
		}

		std::vector<GamePadButtons>* buttons =
			ResolveCanvasGamepadBindings(*canvas, action);
		if (!buttons) {
			return;
		}
		buttons->clear();
		for (int32_t i = 0; bindings && i < count; ++i) {

			const int32_t code = bindings[i];
			const GamePadButtons button = static_cast<GamePadButtons>(code);
			if (0 <= code && code < static_cast<int32_t>(GamePadButtons::Counts) &&
				std::find(buttons->begin(), buttons->end(), button) == buttons->end()) {
				buttons->emplace_back(button);
			}
		}
	}

	void ManagedScriptRuntime::IrisTransitionCommandCallback(
		ManagedNativeEntity entity, int32_t command, float value) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		IrisTransitionComponent* iris = world && world->IsAlive(resolved) ?
			world->TryGetComponent<IrisTransitionComponent>(resolved) : nullptr;
		if (!iris) {
			return;
		}

		const uint64_t beforeSerial = iris->runtimeCommandSerial;
		switch (command) {
		case 0:
			iris->IrisOut(value);
			break;
		case 1:
			iris->IrisIn(value);
			break;
		case 2:
			iris->SetProgress(value);
			break;
		case 3:
			iris->Cancel();
			break;
		case 4:
			iris->Reset();
			break;
		default:
			return;
		}
		if (beforeSerial == iris->runtimeCommandSerial) {
			return;
		}

		const SystemContext* context = GetCurrentContext();
		if (!context || context->mode != WorldMode::Play) {
			return;
		}
		const bool blockInput =
			(command == 0 || command == 1) && iris->blockInput;
		UIRuntimeService::GetInstance().SetTransitionInputBlocked(blockInput);
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

		using OverridesMap = std::unordered_map<std::string, Engine::MaterialParameterValue>;

		// componentTypeとsubMeshIndexから上書き対象のparameterOverridesを集める、0=Mesh 1=Sprite 2=Text 3=Primitive
		std::vector<OverridesMap*> CollectColorTargets(Engine::ECSWorld& world, const Engine::Entity& entity,
			int32_t componentType, int32_t subMeshIndex) {

			std::vector<OverridesMap*> targets;
			if (componentType == 0) {

				if (Engine::MeshRendererComponent* renderer = world.TryGetComponent<Engine::MeshRendererComponent>(entity)) {
					if (subMeshIndex < 0) {
						for (Engine::SubMeshMaterial& subMesh : renderer->subMeshes) {
							targets.emplace_back(&subMesh.parameterOverrides);
						}
					} else if (static_cast<size_t>(subMeshIndex) < renderer->subMeshes.size()) {
						targets.emplace_back(&renderer->subMeshes[static_cast<size_t>(subMeshIndex)].parameterOverrides);
					}
				}
			} else if (componentType == 1) {

				if (Engine::SpriteRendererComponent* renderer = world.TryGetComponent<Engine::SpriteRendererComponent>(entity)) {
					targets.emplace_back(&renderer->parameterOverrides);
				}
			} else if (componentType == 2) {

				if (Engine::TextRendererComponent* renderer = world.TryGetComponent<Engine::TextRendererComponent>(entity)) {
					targets.emplace_back(&renderer->parameterOverrides);
				}
			} else if (componentType == 3) {

				if (Engine::PrimitiveRendererComponent* renderer = world.TryGetComponent<Engine::PrimitiveRendererComponent>(entity)) {
					targets.emplace_back(&renderer->parameterOverrides);
				}
			}
			return targets;
		}

		// shapeIndexからCollisionComponentの衝突形状を取得する、範囲外はnullptr
		Engine::CollisionShape* ResolveCollisionShape(Engine::ECSWorld& world, const Engine::Entity& entity, int32_t shapeIndex) {

			if (!world.IsAlive(entity)) {
				return nullptr;
			}
			Engine::CollisionComponent* collision = world.TryGetComponent<Engine::CollisionComponent>(entity);
			if (!collision || shapeIndex < 0 ||
				static_cast<size_t>(shapeIndex) >= collision->shapes.size()) {
				return nullptr;
			}
			return &collision->shapes[static_cast<size_t>(shapeIndex)];
		}
	}

	void ManagedScriptRuntime::SetRendererMaterialColorCallback(ManagedNativeEntity entity, int32_t componentType,
		int32_t subMeshIndex, const char* param, float r, float g, float b, float a) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return;
		}
		const Entity resolved = ResolveEntity(entity);
		if (!world->IsAlive(resolved)) {
			return;
		}

		// バッファ構築側で宣言成分数へ詰めるのでColor4で保持しfloat3 float4どちらにも対応する
		MaterialParameterValue value{};
		value.value = Color4(r, g, b, a);

		// パラメータ名未指定は標準的なcolor名へフォールバックして設定する、未使用キーは描画側で無視される
		std::vector<std::string> names;
		if (param != nullptr && param[0] != '\0') {
			names.emplace_back(param);
		} else {
			names = { "color", "baseColor", "albedo" };
		}

		for (std::unordered_map<std::string, MaterialParameterValue>* overrides :
			CollectColorTargets(*world, resolved, componentType, subMeshIndex)) {

			for (const std::string& name : names) {
				(*overrides)[name] = value;
			}
		}
	}

	ManagedColor4 ManagedScriptRuntime::GetRendererMaterialColorCallback(ManagedNativeEntity entity,
		int32_t componentType, int32_t subMeshIndex) {

		// 未設定や対象が無い場合は白を返す
		ManagedColor4 result{ 1.0f, 1.0f, 1.0f, 1.0f };
		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return result;
		}
		const Entity resolved = ResolveEntity(entity);
		if (!world->IsAlive(resolved)) {
			return result;
		}

		const std::vector<std::unordered_map<std::string, MaterialParameterValue>*> targets =
			CollectColorTargets(*world, resolved, componentType, subMeshIndex < 0 ? 0 : subMeshIndex);
		if (targets.empty()) {
			return result;
		}

		// 代表として先頭対象から、color名のフォールバック順で最初に見つかった値を返す
		const std::unordered_map<std::string, MaterialParameterValue>& overrides = *targets.front();
		for (const char* name : { "color", "baseColor", "albedo" }) {

			const auto it = overrides.find(name);
			if (it == overrides.end()) {
				continue;
			}
			if (const Color4* c = std::get_if<Color4>(&it->second.value)) { return ManagedColor4{ c->r, c->g, c->b, c->a }; }
			if (const Vector4* v = std::get_if<Vector4>(&it->second.value)) { return ManagedColor4{ v->x, v->y, v->z, v->w }; }
			if (const Vector3* v = std::get_if<Vector3>(&it->second.value)) { return ManagedColor4{ v->x, v->y, v->z, 1.0f }; }
		}
		return result;
	}

	int32_t ManagedScriptRuntime::CollisionShapeCountCallback(ManagedNativeEntity entity) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return 0;
		}
		const Entity resolved = ResolveEntity(entity);
		const CollisionComponent* collision = world->IsAlive(resolved) ?
			world->TryGetComponent<CollisionComponent>(resolved) : nullptr;
		return collision ? static_cast<int32_t>(collision->shapes.size()) : 0;
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
			collision->shapes.emplace_back(CollisionShape{});
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
		if (collision && 0 <= shapeIndex && static_cast<size_t>(shapeIndex) < collision->shapes.size()) {
			collision->shapes.erase(collision->shapes.begin() + shapeIndex);
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
			collision->shapes.clear();
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
		}
		return 0;
	}

	int32_t ManagedScriptRuntime::CollisionSetShapePropertyCallback(ManagedNativeEntity entity,
		int32_t shapeIndex, int32_t propertyId, const void* value, int32_t size) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return 0;
		}
		CollisionShape* shape = ResolveCollisionShape(*world, ResolveEntity(entity), shapeIndex);
		if (!shape || !value) {
			return 0;
		}

		switch (propertyId) {
		case 0: if (size < 4) { return 0; } shape->type = static_cast<ColliderShapeType>(*reinterpret_cast<const int32_t*>(value)); return 1;
		case 1: if (size < 4) { return 0; } shape->enabled = *reinterpret_cast<const int32_t*>(value) != 0; return 1;
		case 2: if (size < 4) { return 0; } shape->isTrigger = *reinterpret_cast<const int32_t*>(value) != 0; return 1;
		case 3: if (size < 4) { return 0; } shape->useTransformRotation = *reinterpret_cast<const int32_t*>(value) != 0; return 1;
		case 4: if (size < 4) { return 0; } shape->rotatedQuad = *reinterpret_cast<const int32_t*>(value) != 0; return 1;
		case 5: if (size < 12) { return 0; } std::memcpy(&shape->offset, value, 12); return 1;
		case 6: if (size < 12) { return 0; } std::memcpy(&shape->rotationDegrees, value, 12); return 1;
		case 7: if (size < 4) { return 0; } std::memcpy(&shape->radius, value, 4); return 1;
		case 8: if (size < 8) { return 0; } std::memcpy(&shape->halfSize2D, value, 8); return 1;
		case 9: if (size < 12) { return 0; } std::memcpy(&shape->halfExtents3D, value, 12); return 1;
		}
		return 0;
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
		anim->runtimeAnimationFinished = false;
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
		line->points.emplace_back(ToLinePoint(point));
		// 追加した点の位置をC#へ返す、UpdatePointの対象指定に使う
		return static_cast<int32_t>(line->points.size() - 1);
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
		// indexが現在の点列範囲外なら更新しない、Clear/SetPoints後の古いindexを弾く
		if (point.index < 0 || static_cast<size_t>(point.index) >= line->points.size()) {
			return;
		}
		line->points[static_cast<size_t>(point.index)] = ToLinePoint(point);
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

		// 対象entityのAudioSourceComponentを取得する、無効entityやcomponent無しはnullptr
		AudioSourceComponent* ResolveAudioSource(ManagedNativeEntity entity) {
			ECSWorld* world = ResolveWorld(entity);
			if (!world) {
				return nullptr;
			}
			const Entity resolved = ResolveEntity(entity);
			return world->IsAlive(resolved) ? world->TryGetComponent<AudioSourceComponent>(resolved) : nullptr;
		}
	}

	void ManagedScriptRuntime::AudioPlayCallback(ManagedNativeEntity entity) {
		if (AudioSourceComponent* audio = ResolveAudioSource(entity)) {
			audio->Play();
		}
	}

	void ManagedScriptRuntime::AudioPlayOneShotCallback(
		ManagedNativeEntity entity, ManagedAssetGUID clipID, float volumeScale) {

		if (AudioSourceComponent* audio = ResolveAudioSource(entity)) {
			audio->PlayOneShot(ToAssetID(clipID), volumeScale);
		}
	}

	void ManagedScriptRuntime::AudioPauseCallback(ManagedNativeEntity entity) {
		if (AudioSourceComponent* audio = ResolveAudioSource(entity)) {
			audio->Pause();
		}
	}

	void ManagedScriptRuntime::AudioUnPauseCallback(ManagedNativeEntity entity) {
		if (AudioSourceComponent* audio = ResolveAudioSource(entity)) {
			audio->UnPause();
		}
	}

	void ManagedScriptRuntime::AudioStopCallback(ManagedNativeEntity entity) {
		if (AudioSourceComponent* audio = ResolveAudioSource(entity)) {
			audio->Stop();
		}
	}

	int32_t ManagedScriptRuntime::AudioIsPlayingCallback(ManagedNativeEntity entity) {
		const AudioSourceComponent* audio = ResolveAudioSource(entity);
		return audio && audio->IsPlaying() ? 1 : 0;
	}

} // Engine
