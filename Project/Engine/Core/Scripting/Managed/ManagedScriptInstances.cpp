#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"
#include "Generated/ManagedComponentBindings.generated.h"
#include <Engine/Core/World/Components/Time/TimeScaleComponent.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ScriptProfiler.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>

// windows
#include <windows.h>
// c++
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <system_error>
#include <string_view>


Engine::ManagedScriptInstanceHandle Engine::ManagedScriptRuntime::CreateInstance(const std::string& scriptTypeID,
	ECSWorld& world, const Entity& entity, const nlohmann::json& serializedFields, uint64_t scriptSlotID) {

	if (!initialized_ || !bridge_.createInstance_) {
		return ManagedScriptInstanceHandle::Null();
	}

	const std::string json = serializedFields.is_object() ? serializedFields.dump() : std::string("{}");
	ManagedScriptInstanceHandle createdHandle = ManagedScriptInstanceHandle::Null();
	// EntityとComponent参照の復元中だけ生成対象のWorldを参照可能にする
	ScopedReferenceWorld worldScope(world);
	const ManagedStatus status = bridge_.createInstance_(scriptTypeID.c_str(), MakeNativeEntity(world, entity), json.c_str(),
		scriptSlotID, &createdHandle);
	if (status == ManagedStatus::Ok && createdHandle.IsValid()) {
		ScriptProfiler::GetInstance().Register({
			ScriptProfiler::OwnerID(createdHandle), MakeNativeEntity(world, entity), scriptSlotID,
			scriptTypeID, GetScriptSchema(scriptTypeID).fullTypeName });
	}
	// 生成失敗時は無効ハンドルを返す
	return status == ManagedStatus::Ok ? createdHandle : ManagedScriptInstanceHandle::Null();
}

void Engine::ManagedScriptRuntime::SetSerializedFields(ManagedScriptInstanceHandle handle, const nlohmann::json& serializedFields) {

	if (!initialized_ || !bridge_.setSerializedFields_ || !handle.IsValid()) {
		return;
	}

	const std::string json = serializedFields.is_object() ? serializedFields.dump() : std::string("{}");
	bridge_.setSerializedFields_(handle, json.c_str());
}

void Engine::ManagedScriptRuntime::FlushPendingReferences(ECSWorld& world) {

	if (!initialized_ || !bridge_.flushPendingReferences_) {
		return;
	}
	ScopedReferenceWorld worldScope(world);
	bridge_.flushPendingReferences_();
}

void Engine::ManagedScriptRuntime::DestroyInstance(ManagedScriptInstanceHandle handle) {

	if (!initialized_ || !bridge_.destroyInstance_ || !handle.IsValid()) {
		return;
	}
	bridge_.destroyInstance_(handle);
	ScriptProfiler::GetInstance().Unregister(handle);
}

void Engine::ManagedScriptRuntime::ConfigureProfiler(const char* typeName, ManagedNativeEntity entity, uint64_t slotID) {

	if (bridge_.configureProfiler_) {
		bridge_.configureProfiler_(typeName, entity, slotID);
	}
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeAwake(ManagedScriptInstanceHandle handle, const SystemContext& context) {
	ScriptProfileScope profile(handle, "Awake");
	return Invoke(bridge_.invokeAwake_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeStart(ManagedScriptInstanceHandle handle, const SystemContext& context) {
	ScriptProfileScope profile(handle, "Start");
	return Invoke(bridge_.invokeStart_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeOnEnable(ManagedScriptInstanceHandle handle, const SystemContext& context) {
	ScriptProfileScope profile(handle, "OnEnable");
	return Invoke(bridge_.invokeOnEnable_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeOnDisable(ManagedScriptInstanceHandle handle, const SystemContext& context) {
	ScriptProfileScope profile(handle, "OnDisable");
	return Invoke(bridge_.invokeOnDisable_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeOnDestroy(ManagedScriptInstanceHandle handle, const SystemContext& context) {
	ScriptProfileScope profile(handle, "OnDestroy");
	return Invoke(bridge_.invokeOnDestroy_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeFixedUpdate(ManagedScriptInstanceHandle handle, const SystemContext& context) {
	ScriptProfileScope profile(handle, "FixedUpdate");
	return Invoke(bridge_.invokeFixedUpdate_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeUpdate(ManagedScriptInstanceHandle handle, const SystemContext& context) {
	ScriptProfileScope profile(handle, "Update");
	return Invoke(bridge_.invokeUpdate_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeLateUpdate(ManagedScriptInstanceHandle handle, const SystemContext& context) {
	ScriptProfileScope profile(handle, "LateUpdate");
	return Invoke(bridge_.invokeLateUpdate_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeCollisionEnter(ManagedScriptInstanceHandle handle,
	const SystemContext& context, const ManagedCollisionEvent& collision) {
	ScriptProfileScope profile(handle, "CollisionEnter");
	return InvokeCollision(bridge_.invokeCollisionEnter_, handle, context, collision);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeCollisionStay(ManagedScriptInstanceHandle handle,
	const SystemContext& context, const ManagedCollisionEvent& collision) {
	ScriptProfileScope profile(handle, "CollisionStay");
	return InvokeCollision(bridge_.invokeCollisionStay_, handle, context, collision);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeCollisionExit(ManagedScriptInstanceHandle handle,
	const SystemContext& context, const ManagedCollisionEvent& collision) {
	ScriptProfileScope profile(handle, "CollisionExit");
	return InvokeCollision(bridge_.invokeCollisionExit_, handle, context, collision);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeAnimationEvent(ManagedScriptInstanceHandle handle,
	const SystemContext& context, const char* name, float floatParam, int32_t intParam, const char* stringParam) {
	ScriptProfileScope profile(handle, "AnimationEvent");

	if (!initialized_ || !bridge_.invokeAnimationEvent_ || !handle.IsValid()) {
		return ManagedStatus::InvalidInstanceHandle;
	}
	FrameProfiler::ScopedSample scriptSample(FrameProfiler::Category::Script);
	// コンテキストはRAIIで設定しC#側で例外が起きても確実に元へ戻す
	ScopedInvocationContext contextScope(context);
	return bridge_.invokeAnimationEvent_(handle, name ? name : "", floatParam, intParam, stringParam ? stringParam : "");
}

nlohmann::json Engine::ManagedScriptRuntime::BuildSerializedValueMap(
	const nlohmann::json& serializedFields) {

	nlohmann::json result = nlohmann::json::object();
	if (!serializedFields.is_object() ||
		!serializedFields.contains("fields") || !serializedFields["fields"].is_object()) {
		return result;
	}

	for (auto& [guid, entry] : serializedFields["fields"].items()) {
		if (entry.is_object() && entry.contains("value")) {
			result[guid] = entry["value"];
		} else {
			result[guid] = entry;
		}
	}
	return result;
}

nlohmann::json Engine::ManagedScriptRuntime::GetRuntimeSerializedState(ManagedScriptInstanceHandle handle) {

	nlohmann::json empty = nlohmann::json::object();
	if (!initialized_ || !bridge_.getRuntimeStateSize_ || !bridge_.copyRuntimeState_ || !handle.IsValid()) {
		return empty;
	}

	int32_t size = 0;
	if (bridge_.getRuntimeStateSize_(handle, &size) != ManagedStatus::Ok || size <= 0) {
		return empty;
	}
	std::string buffer(static_cast<size_t>(size), '\0');
	int32_t written = 0;
	if (bridge_.copyRuntimeState_(handle, buffer.data(), size, &written) != ManagedStatus::Ok) {
		return empty;
	}
	buffer.resize(static_cast<size_t>(written));
	try {
		return nlohmann::json::parse(buffer);
	}
	catch (const nlohmann::json::exception&) {
		return empty;
	}
}

void Engine::ManagedScriptRuntime::SetRuntimeSerializedField(ManagedScriptInstanceHandle handle, ECSWorld& world,
	const std::string& fieldID, const nlohmann::json& value) {

	if (!initialized_ || !bridge_.setRuntimeField_ || !handle.IsValid() || fieldID.empty()) {
		return;
	}
	const std::string valueJson = value.dump();
	ScopedReferenceWorld worldScope(world);
	bridge_.setRuntimeField_(handle, fieldID.c_str(), valueJson.c_str());
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::Invoke(InvokeFn function, ManagedScriptInstanceHandle handle, const SystemContext& context) {

	if (!initialized_ || !function || !handle.IsValid()) {
		return ManagedStatus::InvalidInstanceHandle;
	}
	FrameProfiler::ScopedSample scriptSample(FrameProfiler::Category::Script);
	// コンテキストはRAIIで設定しC#側で例外が起きても確実に元へ戻す
	ScopedInvocationContext contextScope(context);
	return function(handle);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeCollision(InvokeCollisionFn function, ManagedScriptInstanceHandle handle,
	const SystemContext& context, const ManagedCollisionEvent& collision) {

	if (!initialized_ || !function || !handle.IsValid()) {
		return ManagedStatus::InvalidInstanceHandle;
	}
	FrameProfiler::ScopedSample scriptSample(FrameProfiler::Category::Script);
	ScopedInvocationContext contextScope(context);
	return function(handle, collision);
}
