#include "CollisionComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <vector>

namespace {

	// チャンク内設定をjsonへ書き出す
	void SaveSettings(const Engine::CollisionComponent& component, nlohmann::json& out) {

		out["enabled"] = component.enabled;
		out["isStatic"] = component.isStatic;
		out["enablePushback"] = component.enablePushback;
		out["typeMask"] = component.typeMask;
	}
}

//============================================================================
//	CollisionComponent classMethods
//============================================================================
void Engine::CollisionComponent::OnAdded(
	ECSWorld& world, const Entity& entity, [[maybe_unused]] CollisionComponent& component) {

	// 形状列と実行状態は設定Componentから分離し、必要なWorldだけへ構築する
	if (!world.HasBuffer<CollisionShape>(entity)) {
		world.AddBuffer<CollisionShape>(entity);
	}
	EnsureCollisionShapes(world, entity);
	if (world.GetKind() == ECSWorldKind::Runtime &&
		!world.HasComponent<CollisionRuntimeStateComponent>(entity)) {
		world.AddComponent<CollisionRuntimeStateComponent>(entity);
	}
	if (world.GetKind() == ECSWorldKind::Runtime &&
		!world.HasComponent<CollisionCompoundComponent>(entity)) {
		world.AddComponent<CollisionCompoundComponent>(entity);
	}
	RebuildCollisionCompound(world, entity);
}

void Engine::CollisionComponent::OnRemoved(ECSWorld& world, const Entity& entity) {

	// 設定Componentに従属する形状列とRuntime状態を同時に破棄する
	if (world.HasBuffer<CollisionShape>(entity)) {
		world.RemoveBuffer<CollisionShape>(entity);
	}
	if (world.HasComponent<CollisionRuntimeStateComponent>(entity)) {
		world.RemoveComponent<CollisionRuntimeStateComponent>(entity);
	}
	if (world.HasComponent<CollisionCompoundComponent>(entity)) {
		world.RemoveComponent<CollisionCompoundComponent>(entity);
	}
}

void Engine::CollisionComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] CollisionComponent& component) {
}

void Engine::CollisionComponent::ReleaseStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] CollisionComponent& component) {
}

void Engine::CollisionComponent::DeserializeECS(ECSWorld& world, const Entity& entity,
	const nlohmann::json& in, CollisionComponent& component) {

	DeserializeComponent(world, entity, in, component);
}

void Engine::CollisionComponent::SerializeECS(const ECSWorld& world, const Entity& entity,
	const CollisionComponent& component, nlohmann::json& out) {

	SerializeComponent(world, entity, component, out);
}

//============================================================================
//	CollisionCompoundComponent classMethods
//============================================================================
void Engine::CollisionCompoundComponent::OnAdded(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] CollisionCompoundComponent& component) {

	RebuildCollisionCompound(world, entity);
}

void Engine::CollisionCompoundComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] CollisionCompoundComponent& component) {
}

void Engine::CollisionCompoundComponent::ReleaseStorage(
	ECSWorld& world, [[maybe_unused]] const Entity& entity,
	CollisionCompoundComponent& component) {

	if (component.blob.IsValid()) {
		world.GetStorage().Get<BlobStore>().Release(component.blob.handle);
		component.blob = {};
	}
}

void Engine::CollisionCompoundComponent::DeserializeECS(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] const nlohmann::json& in,
	[[maybe_unused]] CollisionCompoundComponent& component) {
}

void Engine::CollisionCompoundComponent::SerializeECS(
	[[maybe_unused]] const ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] const CollisionCompoundComponent& component,
	nlohmann::json& out) {

	out = nlohmann::json::object();
}

std::span<Engine::CollisionShape> Engine::GetCollisionShapes(
	ECSWorld& world, const Entity& entity) {

	DynamicBuffer<CollisionShape> buffer =
		world.TryGetBuffer<CollisionShape>(entity);
	return buffer.GetSpan();
}

std::span<const Engine::CollisionShape> Engine::GetCollisionShapes(
	const ECSWorld& world, const Entity& entity) {

	// RuntimeはDynamicBufferではなく共有Blobを判定の読み取り元にする
	if (world.GetKind() == ECSWorldKind::Runtime) {
		const CollisionCompoundComponent* compound =
			world.TryGetComponent<CollisionCompoundComponent>(entity);
		if (compound && compound->blob.IsValid()) {
			const BlobStore* store = world.GetStorage().TryGet<BlobStore>();
			const CollisionCompoundBlob* root =
				store ? store->TryGetObject<CollisionCompoundBlob>(
					compound->blob.handle) : nullptr;
			if (root) {
				return root->shapes.Get(root);
			}
		}
	}
	return world.GetBufferSpan<CollisionShape>(entity);
}

void Engine::SetCollisionShapes(ECSWorld& world, const Entity& entity,
	std::span<const CollisionShape> shapes) {

	DynamicBuffer<CollisionShape> buffer =
		world.TryGetBuffer<CollisionShape>(entity);
	if (!buffer.IsValid()) {
		buffer = world.AddBuffer<CollisionShape>(entity);
	}
	buffer.Clear();
	buffer.Reserve(static_cast<uint32_t>(shapes.size()));
	for (const CollisionShape& shape : shapes) {
		buffer.Add(shape);
	}
	RebuildCollisionCompound(world, entity);
	world.MarkComponentModified<CollisionShape>(entity);
}

void Engine::EnsureCollisionShapes(ECSWorld& world, const Entity& entity) {

	if (!GetCollisionShapes(world, entity).empty()) {
		return;
	}
	const CollisionShape shape{};
	SetCollisionShapes(world, entity, std::span<const CollisionShape>(&shape, 1));
}

Engine::CollisionShape* Engine::TryGetCollisionShape(
	ECSWorld& world, const Entity& entity, uint32_t index) {

	const std::span<CollisionShape> shapes = GetCollisionShapes(world, entity);
	return index < shapes.size() ? &shapes[index] : nullptr;
}

const Engine::CollisionShape* Engine::TryGetCollisionShape(
	const ECSWorld& world, const Entity& entity, uint32_t index) {

	const std::span<const CollisionShape> shapes = GetCollisionShapes(world, entity);
	return index < shapes.size() ? &shapes[index] : nullptr;
}

void Engine::AddCollisionShape(ECSWorld& world, const Entity& entity,
	const CollisionShape& shape) {

	DynamicBuffer<CollisionShape> buffer =
		world.TryGetBuffer<CollisionShape>(entity);
	if (!buffer.IsValid()) {
		buffer = world.AddBuffer<CollisionShape>(entity);
	}
	buffer.Add(shape);
	RebuildCollisionCompound(world, entity);
	world.MarkComponentModified<CollisionShape>(entity);
}

bool Engine::RemoveCollisionShape(
	ECSWorld& world, const Entity& entity, uint32_t index) {

	DynamicBuffer<CollisionShape> buffer =
		world.TryGetBuffer<CollisionShape>(entity);
	if (!buffer.IsValid() || buffer.GetSize() <= index) {
		return false;
	}
	buffer.RemoveAt(index);
	RebuildCollisionCompound(world, entity);
	world.MarkComponentModified<CollisionShape>(entity);
	return true;
}

void Engine::ClearCollisionShapes(ECSWorld& world, const Entity& entity) {

	DynamicBuffer<CollisionShape> buffer =
		world.TryGetBuffer<CollisionShape>(entity);
	if (buffer.IsValid()) {
		buffer.Clear();
		RebuildCollisionCompound(world, entity);
		world.MarkComponentModified<CollisionShape>(entity);
	}
}

void Engine::RebuildCollisionCompound(ECSWorld& world, const Entity& entity) {

	if (world.GetKind() != ECSWorldKind::Runtime ||
		!world.HasComponent<CollisionCompoundComponent>(entity)) {
		return;
	}

	CollisionCompoundComponent& compound =
		world.GetComponent<CollisionCompoundComponent>(entity);
	BlobStore& store = world.GetStorage().Get<BlobStore>();
	if (compound.blob.IsValid()) {
		store.Release(compound.blob.handle);
		compound.blob = {};
	}

	// 形状列全体を単一Blobへまとめ、同じCollider構成は内容Hashで共有する
	const std::span<const CollisionShape> shapes =
		world.GetBufferSpan<CollisionShape>(entity);
	BlobBuilder<CollisionCompoundBlob> builder{};
	CollisionCompoundBlob root{};
	root.shapes = builder.AddArray(shapes);
	builder.SetRoot(root);
	compound.blob = builder.Build(store);
}

bool Engine::IsCollisionColliding(const ECSWorld& world, const Entity& entity) {

	const CollisionRuntimeStateComponent* state =
		world.TryGetComponent<CollisionRuntimeStateComponent>(entity);
	return state && state->colliding;
}

void Engine::DeserializeComponent(ECSWorld& world, const Entity& entity,
	const nlohmann::json& in, CollisionComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
	component.isStatic = in.value("isStatic", component.isStatic);
	component.enablePushback = in.value("enablePushback", component.enablePushback);
	component.typeMask = in.value("typeMask", component.typeMask);

	std::vector<CollisionShape> shapes;
	if (in.contains("shapes") && in["shapes"].is_array()) {
		shapes.reserve(in["shapes"].size());
		for (const nlohmann::json& shapeJson : in["shapes"]) {
			shapes.emplace_back(shapeJson.get<CollisionShape>());
		}
	}
	if (shapes.empty()) {
		shapes.emplace_back();
	}
	SetCollisionShapes(world, entity, shapes);
}

void Engine::SerializeComponent(const ECSWorld& world, const Entity& entity,
	const CollisionComponent& component, nlohmann::json& out) {

	SerializeCollisionDraft(component, GetCollisionShapes(world, entity), out);
}

void Engine::SerializeCollisionDraft(const CollisionComponent& component,
	std::span<const CollisionShape> shapes, nlohmann::json& out) {

	SaveSettings(component, out);
	out["shapes"] = nlohmann::json::array();
	for (const CollisionShape& shape : shapes) {
		out["shapes"].push_back(shape);
	}
}

void Engine::SerializeComponentDraft(
	const CollisionComponent& component, nlohmann::json& out) {

	SaveSettings(component, out);
	out["shapes"] = nlohmann::json::array();
}
