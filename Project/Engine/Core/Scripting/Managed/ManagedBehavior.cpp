#include "ManagedBehavior.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionTypes.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptUtility.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

//============================================================================
//	ManagedBehavior classMethods
//============================================================================
namespace {

	// CollisionContactをC#共有型へ変換する
	// Entity変換はManagedScriptUtilityのレジストリ経由実装を共有する
	Engine::ManagedCollisionEvent ToManagedCollision(Engine::ECSWorld& world, const Engine::CollisionContact& collision) {

		Engine::ManagedCollisionEvent managed{};
		managed.self = Engine::MakeNativeEntity(world, collision.self);
		managed.other = Engine::MakeNativeEntity(world, collision.other);
		managed.normal = Engine::ToManagedVector3(collision.normal);
		managed.point = Engine::ToManagedVector3(collision.point);
		managed.penetration = collision.penetration;
		managed.selfShapeIndex = static_cast<int32_t>(collision.selfShapeIndex);
		managed.otherShapeIndex = static_cast<int32_t>(collision.otherShapeIndex);
		managed.isTrigger = collision.trigger ? 1 : 0;
		return managed;
	}
}

Engine::ManagedBehavior::ManagedBehavior(std::string scriptTypeId, std::string displayName) :
	scriptTypeId_(std::move(scriptTypeId)), displayName_(std::move(displayName)) {
}

void Engine::ManagedBehavior::SetSerializedFields(const nlohmann::json& serializedFields) {

	if (serializedFields.is_object()) {
		if (serializedFields_ == serializedFields) {
			return;
		}
		serializedFields_ = serializedFields;
	} else {
		if (serializedFields_.is_object() && serializedFields_.empty()) {
			return;
		}
		serializedFields_ = nlohmann::json::object();
	}

	// 生成済みのC#インスタンスには、Play中のInspector変更をその場で反映する
	if (managedHandle_.IsValid()) {
		ManagedScriptRuntime::GetInstance().SetSerializedFields(managedHandle_, serializedFields_);
	}
}

void Engine::ManagedBehavior::Awake([[maybe_unused]] ECSWorld& world, const SystemContext& context, const Entity& entity) {

	// インスタンス生成はライフサイクルのPass1(EnsureInstance)で済ませてある
	if (!managedHandle_.IsValid() || faulted_) {
		return;
	}
	HandleStatus(ManagedScriptRuntime::GetInstance().InvokeAwake(managedHandle_, context), "Awake", entity);
}

void Engine::ManagedBehavior::Start([[maybe_unused]] ECSWorld& world,
	const SystemContext& context, const Entity& entity) {

	if (!managedHandle_.IsValid() || faulted_) {
		return;
	}
	HandleStatus(ManagedScriptRuntime::GetInstance().InvokeStart(managedHandle_, context), "Start", entity);
}

void Engine::ManagedBehavior::OnEnable([[maybe_unused]] ECSWorld& world,
	const SystemContext& context, const Entity& entity) {

	if (!managedHandle_.IsValid() || faulted_) {
		return;
	}
	HandleStatus(ManagedScriptRuntime::GetInstance().InvokeOnEnable(managedHandle_, context), "OnEnable", entity);
}

void Engine::ManagedBehavior::OnDisable([[maybe_unused]] ECSWorld& world,
	const SystemContext& context, const Entity& entity) {

	if (!managedHandle_.IsValid() || faulted_) {
		return;
	}
	HandleStatus(ManagedScriptRuntime::GetInstance().InvokeOnDisable(managedHandle_, context), "OnDisable", entity);
}

void Engine::ManagedBehavior::OnDestroy([[maybe_unused]] ECSWorld& world,
	const SystemContext& context, const Entity& entity) {

	if (!managedHandle_.IsValid()) {
		return;
	}
	// faultedでなければOnDestroyを通知する。faulted時はgameplay callbackを呼ばず解放だけ行う
	if (!faulted_) {
		HandleStatus(ManagedScriptRuntime::GetInstance().InvokeOnDestroy(managedHandle_, context), "OnDestroy", entity);
	}
	ManagedScriptRuntime::GetInstance().DestroyInstance(managedHandle_);
	managedHandle_ = ManagedScriptInstanceHandle::Null();
}

void Engine::ManagedBehavior::FixedUpdate([[maybe_unused]] ECSWorld& world,
	const SystemContext& context, const Entity& entity) {

	if (!managedHandle_.IsValid() || faulted_) {
		return;
	}
	HandleStatus(ManagedScriptRuntime::GetInstance().InvokeFixedUpdate(managedHandle_, context), "FixedUpdate", entity);
}

void Engine::ManagedBehavior::Update([[maybe_unused]] ECSWorld& world,
	const SystemContext& context, const Entity& entity) {

	if (!managedHandle_.IsValid() || faulted_) {
		return;
	}
	HandleStatus(ManagedScriptRuntime::GetInstance().InvokeUpdate(managedHandle_, context), "Update", entity);
}

void Engine::ManagedBehavior::LateUpdate([[maybe_unused]] ECSWorld& world,
	const SystemContext& context, const Entity& entity) {

	if (!managedHandle_.IsValid() || faulted_) {
		return;
	}
	HandleStatus(ManagedScriptRuntime::GetInstance().InvokeLateUpdate(managedHandle_, context), "LateUpdate", entity);
}

void Engine::ManagedBehavior::OnCollisionEnter(ECSWorld& world,
	const SystemContext& context, const CollisionContact& collision) {

	if (!managedHandle_.IsValid() || faulted_) {
		return;
	}

	// C#側のOnCollisionEnterへ渡す
	HandleStatus(ManagedScriptRuntime::GetInstance().InvokeCollisionEnter(managedHandle_, context,
		ToManagedCollision(world, collision)), "OnCollisionEnter", collision.self);
}

void Engine::ManagedBehavior::OnCollisionStay(ECSWorld& world,
	const SystemContext& context, const CollisionContact& collision) {

	if (!managedHandle_.IsValid() || faulted_) {
		return;
	}

	// C#側のOnCollisionStayへ渡す
	HandleStatus(ManagedScriptRuntime::GetInstance().InvokeCollisionStay(managedHandle_, context,
		ToManagedCollision(world, collision)), "OnCollisionStay", collision.self);
}

void Engine::ManagedBehavior::OnCollisionExit(ECSWorld& world,
	const SystemContext& context, const CollisionContact& collision) {

	if (!managedHandle_.IsValid() || faulted_) {
		return;
	}

	// C#側のOnCollisionExitへ渡す
	HandleStatus(ManagedScriptRuntime::GetInstance().InvokeCollisionExit(managedHandle_, context,
		ToManagedCollision(world, collision)), "OnCollisionExit", collision.self);
}

bool Engine::ManagedBehavior::EnsureInstance(ECSWorld& world, const Entity& entity) {

	EnsureCreated(world, entity);
	// 生成に失敗した場合は無効ハンドルのまま。呼び出し側はfaulted扱いにする
	return managedHandle_.IsValid();
}

void Engine::ManagedBehavior::EnsureCreated(ECSWorld& world, const Entity& entity) {

	if (managedHandle_.IsValid()) {
		return;
	}
	managedHandle_ = ManagedScriptRuntime::GetInstance().CreateInstance(scriptTypeId_, world, entity, serializedFields_);
}

void Engine::ManagedBehavior::HandleStatus(ManagedStatus status, const char* callbackName, const Entity& entity) {

	if (status == ManagedStatus::Ok) {
		return;
	}
	// C#側でユーザーcallbackが例外を投げた場合のみfaulted化する。
	// (詳細な例外全文はC#側GuardInstanceがログ済み。ここでは型・callback・entityを残す)
	if (status == ManagedStatus::ScriptException && !faulted_) {

		faulted_ = true;
		Logger::Output(LogType::GameLogic, spdlog::level::err,
			"ManagedBehavior: script faulted and will be disabled. type={} scriptTypeId={} callback={} entity={}:{}",
			displayName_, scriptTypeId_, callbackName, entity.index, entity.generation);
	}
}
