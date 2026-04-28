#include "ManagedBehavior.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Collision/CollisionTypes.h>
#include <Engine/Core/Scripting/ManagedScriptRuntime.h>

//============================================================================
//	ManagedBehavior classMethods
//============================================================================

namespace {

	// C++側EntityをC#側へ渡す参照へ変換する
	Engine::ManagedNativeEntity MakeNativeEntity(Engine::ECSWorld& world, const Engine::Entity& entity) {

		Engine::ManagedNativeEntity native{};
		native.world = reinterpret_cast<std::uintptr_t>(&world);
		native.index = entity.index;
		native.generation = entity.generation;
		return native;
	}

	// Vector3をC#共有型へ変換する
	Engine::ManagedVector3 ToManagedVector3(const Engine::Vector3& value) {

		return Engine::ManagedVector3{ value.x, value.y, value.z };
	}

	// CollisionContactをC#共有型へ変換する
	Engine::ManagedCollisionEvent ToManagedCollision(Engine::ECSWorld& world, const Engine::CollisionContact& collision) {

		Engine::ManagedCollisionEvent managed{};
		managed.self = MakeNativeEntity(world, collision.self);
		managed.other = MakeNativeEntity(world, collision.other);
		managed.normal = ToManagedVector3(collision.normal);
		managed.point = ToManagedVector3(collision.point);
		managed.penetration = collision.penetration;
		managed.selfShapeIndex = static_cast<int32_t>(collision.selfShapeIndex);
		managed.otherShapeIndex = static_cast<int32_t>(collision.otherShapeIndex);
		managed.isTrigger = collision.trigger ? 1 : 0;
		return managed;
	}
}

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

void Engine::ManagedBehavior::OnCollisionEnter(ECSWorld& world,
	const SystemContext& context, const CollisionContact& collision) {

	if (managedHandle_ == 0) {
		return;
	}

	// C#側のOnCollisionEnterへ渡す
	ManagedScriptRuntime::GetInstance().InvokeCollisionEnter(managedHandle_, context, ToManagedCollision(world, collision));
}

void Engine::ManagedBehavior::OnCollisionStay(ECSWorld& world,
	const SystemContext& context, const CollisionContact& collision) {

	if (managedHandle_ == 0) {
		return;
	}

	// C#側のOnCollisionStayへ渡す
	ManagedScriptRuntime::GetInstance().InvokeCollisionStay(managedHandle_, context, ToManagedCollision(world, collision));
}

void Engine::ManagedBehavior::OnCollisionExit(ECSWorld& world,
	const SystemContext& context, const CollisionContact& collision) {

	if (managedHandle_ == 0) {
		return;
	}

	// C#側のOnCollisionExitへ渡す
	ManagedScriptRuntime::GetInstance().InvokeCollisionExit(managedHandle_, context, ToManagedCollision(world, collision));
}

void Engine::ManagedBehavior::EnsureCreated(ECSWorld& world, const Entity& entity) {

	if (managedHandle_ != 0) {
		return;
	}
	managedHandle_ = ManagedScriptRuntime::GetInstance().CreateInstance(typeName_, world, entity, serializedFields_);
}
