#include "ManagedScriptRuntime.h"

//============================================================================
//	include
//============================================================================
#include "ManagedScriptUtility.h"
#include "ScriptFieldStorage.h"
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ScriptProfiler.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedScriptExceptionStore.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <cstdint>
#include <string>
#include <utility>
// json
#include <json.hpp>

//============================================================================
//	ManagedScriptRuntime instanceMethods
//============================================================================

Engine::ManagedScriptInstanceHandle Engine::ManagedScriptRuntime::CreateInstance(const std::string& scriptTypeID,
	ECSWorld& world, const Entity& entity, const nlohmann::json& serializedFields, uint64_t scriptSlotID) {

	if (!initialized_ || !bridge_.createInstance_) {
		return ManagedScriptInstanceHandle::Null();
	}

	const std::string json = serializedFields.is_object() ? serializedFields.dump() : std::string("{}");
	ManagedScriptInstanceHandle createdHandle = ManagedScriptInstanceHandle::Null();
	// EntityとComponent参照の復元中だけ生成対象のWorldを参照可能にする
	ScopedReferenceWorld worldScope(world);
	const ManagedStatus status = bridge_.createInstance_(
		scriptTypeID.c_str(), MakeNativeEntity(world, entity), json.c_str(), scriptSlotID, &createdHandle);
	if (status == ManagedStatus::Ok && createdHandle.IsValid()) {
		ScriptProfiler::GetInstance().Register({ScriptProfiler::OwnerID(createdHandle), MakeNativeEntity(world, entity),
			scriptSlotID, scriptTypeID, GetScriptSchema(scriptTypeID).fullTypeName});
	}
	if (status != ManagedStatus::Ok || !createdHandle.IsValid()) {
		Logger::Output(LogType::Engine, spdlog::level::err, "Scriptの生成に失敗しました type={} slot={} status={}",
			scriptTypeID, scriptSlotID, static_cast<int32_t>(status));
	}
	// 生成失敗時は無効ハンドルを返す
	return status == ManagedStatus::Ok ? createdHandle : ManagedScriptInstanceHandle::Null();
}

void Engine::ManagedScriptRuntime::SetSerializedFields(
	ManagedScriptInstanceHandle handle, const nlohmann::json& serializedFields) {

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

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeAwake(
	ManagedScriptInstanceHandle handle, const SystemContext& context) {

	// 計測を付けてAwakeを実行
	ScriptProfileScope profile(handle, "Awake");
	return Invoke(bridge_.invokeAwake_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeStart(
	ManagedScriptInstanceHandle handle, const SystemContext& context) {

	// 計測を付けてStartを実行
	ScriptProfileScope profile(handle, "Start");
	return Invoke(bridge_.invokeStart_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeOnEnable(
	ManagedScriptInstanceHandle handle, const SystemContext& context) {

	// 計測を付けてOnEnableを実行
	ScriptProfileScope profile(handle, "OnEnable");
	return Invoke(bridge_.invokeOnEnable_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeOnDisable(
	ManagedScriptInstanceHandle handle, const SystemContext& context) {

	// 計測を付けてOnDisableを実行
	ScriptProfileScope profile(handle, "OnDisable");
	return Invoke(bridge_.invokeOnDisable_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeOnDestroy(
	ManagedScriptInstanceHandle handle, const SystemContext& context) {

	// 計測を付けてOnDestroyを実行
	ScriptProfileScope profile(handle, "OnDestroy");
	return Invoke(bridge_.invokeOnDestroy_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeFixedUpdate(
	ManagedScriptInstanceHandle handle, const SystemContext& context) {

	// 計測を付けてFixedUpdateを実行
	ScriptProfileScope profile(handle, "FixedUpdate");
	return Invoke(bridge_.invokeFixedUpdate_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeUpdate(
	ManagedScriptInstanceHandle handle, const SystemContext& context) {

	// 計測を付けてUpdateを実行
	ScriptProfileScope profile(handle, "Update");
	return Invoke(bridge_.invokeUpdate_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeLateUpdate(
	ManagedScriptInstanceHandle handle, const SystemContext& context) {

	// 計測を付けてLateUpdateを実行
	ScriptProfileScope profile(handle, "LateUpdate");
	return Invoke(bridge_.invokeLateUpdate_, handle, context);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeCollisionEnter(
	ManagedScriptInstanceHandle handle, const SystemContext& context, const ManagedCollisionEvent& collision) {

	// 計測を付けて衝突通知を実行
	ScriptProfileScope profile(handle, "CollisionEnter");
	return InvokeCollision(bridge_.invokeCollisionEnter_, handle, context, collision);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeCollisionStay(
	ManagedScriptInstanceHandle handle, const SystemContext& context, const ManagedCollisionEvent& collision) {

	// 計測を付けて衝突通知を実行
	ScriptProfileScope profile(handle, "CollisionStay");
	return InvokeCollision(bridge_.invokeCollisionStay_, handle, context, collision);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeCollisionExit(
	ManagedScriptInstanceHandle handle, const SystemContext& context, const ManagedCollisionEvent& collision) {

	// 計測を付けて衝突通知を実行
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
	const uint64_t reportSequence = ManagedScriptExceptionStore::GetInstance().ReportSequence();
	return CompleteManagedInvocation(
		bridge_.invokeAnimationEvent_(handle, name ? name : "", floatParam, intParam, stringParam ? stringParam : ""),
		reportSequence);
}

nlohmann::json Engine::ManagedScriptRuntime::BuildSerializedValueMap(const nlohmann::json& serializedFields) {

	return ScriptFieldStorage::ExtractValues(serializedFields);
}

nlohmann::json Engine::ManagedScriptRuntime::GetRuntimeSerializedState(ManagedScriptInstanceHandle handle) {

	if (!initialized_ || !bridge_.getRuntimeStateSize_ || !bridge_.copyRuntimeState_ || !handle.IsValid()) {
		return nlohmann::json::object();
	}

	// 確定した実行値の必要容量を取得
	int32_t size = 0;
	if (bridge_.getRuntimeStateSize_(handle, &size) != ManagedStatus::Ok || size <= 0) {
		return nlohmann::json::object();
	}
	// 必要な容量で実行値を受け取る
	std::string buffer(static_cast<size_t>(size), '\0');
	int32_t written = 0;
	if (bridge_.copyRuntimeState_(handle, buffer.data(), size, &written) != ManagedStatus::Ok) {
		return nlohmann::json::object();
	}
	buffer.resize(static_cast<size_t>(written));
	try {
		return nlohmann::json::parse(buffer);
	} catch (const nlohmann::json::exception&) {
		return nlohmann::json::object();
	}
}

bool Engine::ManagedScriptRuntime::CaptureSavedValueMap(
	ManagedScriptInstanceHandle handle, ECSWorld& world, nlohmann::json& fields) {

	if (!initialized_ || !bridge_.getSavedStateSize_ || !bridge_.copySavedState_ || !handle.IsValid()) {
		return false;
	}
	ScopedReferenceWorld worldScope(world);
	int32_t size = 0;
	if (bridge_.getSavedStateSize_(handle, &size) != ManagedStatus::Ok || size <= 0) {
		return false;
	}
	std::string buffer(static_cast<size_t>(size), '\0');
	int32_t written = 0;
	if (bridge_.copySavedState_(handle, buffer.data(), size, &written) != ManagedStatus::Ok || written <= 0 || written > size) {
		return false;
	}
	try {
		// 正しい保存形式を取得できた場合だけ差し替える
		auto candidate = nlohmann::json::parse(buffer.begin(), buffer.begin() + written);
		if (!candidate.is_object()) {
			return false;
		}
		fields = std::move(candidate);
		return true;
	} catch (const nlohmann::json::exception&) {
		return false;
	}
}

bool Engine::ManagedScriptRuntime::CaptureReloadValueMap(
	ManagedScriptInstanceHandle handle, ECSWorld& world, nlohmann::json& fields) {

	if (!initialized_ || !bridge_.getReloadStateSize_ || !bridge_.copyReloadState_ || !handle.IsValid()) {
		return false;
	}
	ScopedReferenceWorld worldScope(world);
	int32_t size = 0;
	if (bridge_.getReloadStateSize_(handle, &size) != ManagedStatus::Ok || size <= 0) {
		return false;
	}
	std::string buffer(static_cast<size_t>(size), '\0');
	int32_t written = 0;
	if (bridge_.copyReloadState_(handle, buffer.data(), size, &written) != ManagedStatus::Ok || written <= 0 ||
		written > size) {
		return false;
	}
	try {
		nlohmann::json candidate = nlohmann::json::parse(buffer.begin(), buffer.begin() + written);
		if (!candidate.is_object()) {
			return false;
		}
		fields = std::move(candidate);
		return true;
	} catch (const nlohmann::json::exception&) {
		return false;
	}
}

bool Engine::ManagedScriptRuntime::ApplyReloadValueMap(
	ManagedScriptInstanceHandle handle, ECSWorld& world, const nlohmann::json& fields) {

	if (!initialized_ || !bridge_.applyReloadState_ || !handle.IsValid() || !fields.is_object()) {
		return false;
	}
	const std::string state = fields.dump();
	ScopedReferenceWorld worldScope(world);
	return bridge_.applyReloadState_(handle, state.c_str()) == ManagedStatus::Ok;
}

void Engine::ManagedScriptRuntime::SetRuntimeSerializedField(
	ManagedScriptInstanceHandle handle, ECSWorld& world, const std::string& fieldID, const nlohmann::json& value) {

	if (!initialized_ || !bridge_.setRuntimeField_ || !handle.IsValid() || fieldID.empty()) {
		return;
	}
	// 変更値をJSONへ変換して参照Worldを設定
	const std::string valueJSON = value.dump();
	ScopedReferenceWorld worldScope(world);
	bridge_.setRuntimeField_(handle, fieldID.c_str(), valueJSON.c_str());
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::Invoke(
	InvokeFn function, ManagedScriptInstanceHandle handle, const SystemContext& context) {

	if (!initialized_ || !function || !handle.IsValid()) {
		return ManagedStatus::InvalidInstanceHandle;
	}
	FrameProfiler::ScopedSample scriptSample(FrameProfiler::Category::Script);
	// コンテキストはRAIIで設定しC#側で例外が起きても確実に元へ戻す
	ScopedInvocationContext contextScope(context);
	const uint64_t reportSequence = ManagedScriptExceptionStore::GetInstance().ReportSequence();
	return CompleteManagedInvocation(function(handle), reportSequence);
}

Engine::ManagedStatus Engine::ManagedScriptRuntime::InvokeCollision(InvokeCollisionFn function,
	ManagedScriptInstanceHandle handle, const SystemContext& context, const ManagedCollisionEvent& collision) {

	if (!initialized_ || !function || !handle.IsValid()) {
		return ManagedStatus::InvalidInstanceHandle;
	}
	FrameProfiler::ScopedSample scriptSample(FrameProfiler::Category::Script);
	ScopedInvocationContext contextScope(context);
	const uint64_t reportSequence = ManagedScriptExceptionStore::GetInstance().ReportSequence();
	return CompleteManagedInvocation(function(handle, collision), reportSequence);
}
