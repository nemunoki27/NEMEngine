#include "ManagedBehavior.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/ManagedScriptRuntime.h>

//============================================================================
//	ManagedBehavior classMethods
//============================================================================

Engine::ManagedBehavior::ManagedBehavior(std::string typeName) :
	typeName_(std::move(typeName)) {
}

void Engine::ManagedBehavior::SetSerializedFields(const nlohmann::json& serializedFields) {

	nlohmann::json nextFields = serializedFields.is_object() ? serializedFields : nlohmann::json::object();
	if (serializedFields_ == nextFields) {
		return;
	}

	serializedFields_ = std::move(nextFields);

	// 生成済みのC#インスタンスには、Play中のInspector変更をその場で反映する
	if (managedHandle_ != 0) {
		ManagedScriptRuntime::GetInstance().SetSerializedFields(managedHandle_, serializedFields_);
	}
}

void Engine::ManagedBehavior::Awake(ECSWorld& world, const SystemContext& context, const Entity& entity) {

	EnsureCreated(world, entity);
	if (managedHandle_ == 0) {
		return;
	}
	ManagedScriptRuntime::GetInstance().InvokeAwake(managedHandle_, context);
}

void Engine::ManagedBehavior::Start([[maybe_unused]] ECSWorld& world,
	const SystemContext& context, [[maybe_unused]] const Entity& entity) {

	if (managedHandle_ == 0) {
		return;
	}
	ManagedScriptRuntime::GetInstance().InvokeStart(managedHandle_, context);
}

void Engine::ManagedBehavior::OnEnable([[maybe_unused]] ECSWorld& world,
	const SystemContext& context, [[maybe_unused]] const Entity& entity) {

	if (managedHandle_ == 0) {
		return;
	}
	ManagedScriptRuntime::GetInstance().InvokeOnEnable(managedHandle_, context);
}

void Engine::ManagedBehavior::OnDisable([[maybe_unused]] ECSWorld& world,
	const SystemContext& context, [[maybe_unused]] const Entity& entity) {

	if (managedHandle_ == 0) {
		return;
	}
	ManagedScriptRuntime::GetInstance().InvokeOnDisable(managedHandle_, context);
}

void Engine::ManagedBehavior::OnDestroy([[maybe_unused]] ECSWorld& world,
	const SystemContext& context, [[maybe_unused]] const Entity& entity) {

	if (managedHandle_ == 0) {
		return;
	}
	ManagedScriptRuntime::GetInstance().InvokeOnDestroy(managedHandle_, context);
	ManagedScriptRuntime::GetInstance().DestroyInstance(managedHandle_);
	managedHandle_ = 0;
}

void Engine::ManagedBehavior::FixedUpdate([[maybe_unused]] ECSWorld& world,
	const SystemContext& context, [[maybe_unused]] const Entity& entity) {

	if (managedHandle_ == 0) {
		return;
	}
	ManagedScriptRuntime::GetInstance().InvokeFixedUpdate(managedHandle_, context);
}

void Engine::ManagedBehavior::Update([[maybe_unused]] ECSWorld& world,
	const SystemContext& context, [[maybe_unused]] const Entity& entity) {

	if (managedHandle_ == 0) {
		return;
	}
	ManagedScriptRuntime::GetInstance().InvokeUpdate(managedHandle_, context);
}

void Engine::ManagedBehavior::LateUpdate([[maybe_unused]] ECSWorld& world,
	const SystemContext& context, [[maybe_unused]] const Entity& entity) {

	if (managedHandle_ == 0) {
		return;
	}
	ManagedScriptRuntime::GetInstance().InvokeLateUpdate(managedHandle_, context);
}

void Engine::ManagedBehavior::EnsureCreated(ECSWorld& world, const Entity& entity) {

	if (managedHandle_ != 0) {
		return;
	}
	managedHandle_ = ManagedScriptRuntime::GetInstance().CreateInstance(typeName_, world, entity, serializedFields_);
}
