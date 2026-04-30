#include "HierarchySystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>

// c++
#include <algorithm>

//============================================================================
//	HierarchySystem classMethods
//============================================================================

namespace {

	// エンティティにシーンオブジェクトコンポーネントがなければ付けて、ローカルIDがなければ新しく生成して返す
	Engine::SceneObjectComponent& EnsureSceneObject(Engine::ECSWorld& world, const Engine::Entity& entity) {

		// シーンオブジェクトが存在しないなら付ける
		if (!world.HasComponent<Engine::SceneObjectComponent>(entity)) {

			auto& sceneObject = world.AddComponent<Engine::SceneObjectComponent>(entity);
			sceneObject.localFileID = Engine::UUID::New();
			sceneObject.activeSelf = true;
			sceneObject.activeInHierarchy = true;
		}

		// ローカルIDが存在しないなら新しく生成する
		auto& sceneObject = world.GetComponent<Engine::SceneObjectComponent>(entity);
		if (!sceneObject.localFileID) {

			sceneObject.localFileID = Engine::UUID::New();
		}
		return sceneObject;
	}

	// 保存されている兄弟順に合わせて親の子リンクを並べ直す
	void SortChildLinksBySiblingOrder(Engine::ECSWorld& world, const Engine::Entity& parent) {

		if (!world.IsAlive(parent) || !world.HasComponent<Engine::HierarchyComponent>(parent)) {
			return;
		}

		auto& parentHierarchy = world.GetComponent<Engine::HierarchyComponent>(parent);
		std::vector<Engine::Entity> children;
		for (Engine::Entity child = parentHierarchy.firstChild; child.IsValid() && world.IsAlive(child);) {

			children.emplace_back(child);
			if (!world.HasComponent<Engine::HierarchyComponent>(child)) {
				break;
			}
			child = world.GetComponent<Engine::HierarchyComponent>(child).nextSibling;
		}
		if (children.size() < 2) {
			return;
		}

		std::stable_sort(children.begin(), children.end(), [&](const Engine::Entity& lhs, const Engine::Entity& rhs) {
			return world.GetComponent<Engine::HierarchyComponent>(lhs).siblingOrder <
				world.GetComponent<Engine::HierarchyComponent>(rhs).siblingOrder;
			});

		parentHierarchy.firstChild = children.front();
		parentHierarchy.lastChild = children.back();
		for (size_t i = 0; i < children.size(); ++i) {

			auto& childHierarchy = world.GetComponent<Engine::HierarchyComponent>(children[i]);
			childHierarchy.prevSibling = (i == 0) ? Engine::Entity::Null() : children[i - 1];
			childHierarchy.nextSibling = (i + 1 < children.size()) ? children[i + 1] : Engine::Entity::Null();
			childHierarchy.siblingOrder = static_cast<int32_t>(i);
		}
	}
}

void Engine::HierarchySystem::OnWorldEnter(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	// ワールド内の全生存エンティティをスコープにしてUUIDからランタイムリンクを再構築
	std::vector<Entity> scope;
	scope.reserve(world.GetRecordCount());
	world.ForEachAliveEntity([&](Entity entity) {
		scope.emplace_back(entity);
		});
	// UUIDからランタイム実行用の親子関係を構築する
	RebuildRuntimeLinks(world, scope);
}

void Engine::HierarchySystem::RebuildRuntimeLinks(ECSWorld& world, const std::vector<Entity>& scope) {

	// ローカルIDとシーンインスタンスIDの組み合わせをキーにしてエンティティを検索するためのマップ
	struct LocalKey {

		// シーンでの永続IDと、シーンインスタンスIDの組み合わせをキーにする
		UUID sceneInstanceID{};
		UUID localFileID{};

		bool operator==(const LocalKey& rhs) const noexcept {
			return sceneInstanceID == rhs.sceneInstanceID && localFileID == rhs.localFileID;
		}
	};
	struct LocalKeyHash {
		size_t operator()(const LocalKey& key) const noexcept {
			size_t h1 = std::hash<UUID>{}(key.sceneInstanceID);
			size_t h2 = std::hash<UUID>{}(key.localFileID);
			return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
		}
	};

	// ローカルIDとシーンインスタンスIDの組み合わせをキーにしてエンティティを検索するためのマップ
	std::unordered_map<LocalKey, Entity, LocalKeyHash> entityMap;
	entityMap.reserve(scope.size());

	// スコープ内のエンティティを全て初期化してから、親子関係を構築する
	for (const auto& entity : scope) {

		if (!world.IsAlive(entity) || !world.HasComponent<HierarchyComponent>(entity)) {
			continue;
		}

		// 親子関係を初期化する
		auto& hierarchy = world.GetComponent<HierarchyComponent>(entity);
		hierarchy.parent = Entity::Null();
		hierarchy.firstChild = Entity::Null();
		hierarchy.lastChild = Entity::Null();
		hierarchy.nextSibling = Entity::Null();
		hierarchy.prevSibling = Entity::Null();

		// ローカルIDとシーンインスタンスIDの組み合わせをキーにしてエンティティを検索するためのマップに登録する
		if (world.HasComponent<SceneObjectComponent>(entity)) {
			const auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
			if (sceneObject.localFileID) {

				entityMap[{ sceneObject.sceneInstanceID, sceneObject.localFileID }] = entity;
			}
		}
	}
	for (const auto& entity : scope) {

		if (!world.IsAlive(entity) || !world.HasComponent<HierarchyComponent>(entity)) {
			continue;
		}
		if (!world.HasComponent<SceneObjectComponent>(entity)) {
			continue;
		}

		auto& hierarchy = world.GetComponent<HierarchyComponent>(entity);
		if (!hierarchy.parentLocalFileID) {
			continue;
		}

		const auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
		auto it = entityMap.find({ sceneObject.sceneInstanceID, hierarchy.parentLocalFileID });
		if (it == entityMap.end()) {
			continue;
		}

		Entity parent = it->second;
		if (!world.IsAlive(parent)) {
			continue;
		}

		if (!world.HasComponent<HierarchyComponent>(parent)) {
			world.AddComponent<HierarchyComponent>(parent);
		}
		AttachLast(world, entity, parent);
	}

	for (const auto& entity : scope) {

		SortChildLinksBySiblingOrder(world, entity);
	}

	// 階層内のアクティブをルート以下で再計算する
	RefreshAllActiveStates(world, scope);
}

void Engine::HierarchySystem::RefreshAllActiveStates(ECSWorld& world, const std::vector<Entity>& scope) {

	for (const auto& entity : scope) {

		if (!world.IsAlive(entity)) {
			continue;
		}
		bool isRoot = true;
		if (world.HasComponent<HierarchyComponent>(entity)) {

			const auto& hierarchy = world.GetComponent<HierarchyComponent>(entity);
			isRoot = !world.IsAlive(hierarchy.parent);
		}
		// ルートエンティティなら、階層内のアクティブをルート以下で再計算する
		if (isRoot) {

			RefreshActiveTree(world, entity);
		}
	}
}

void Engine::HierarchySystem::RefreshActiveTree(ECSWorld& world, const Entity& root) {

	if (!world.IsAlive(root)) {
		return;
	}
	bool parentActive = true;
	if (world.HasComponent<HierarchyComponent>(root)) {

		const auto& hierarchy = world.GetComponent<HierarchyComponent>(root);
		if (world.IsAlive(hierarchy.parent)) {

			parentActive = EnsureSceneObject(world, hierarchy.parent).activeInHierarchy;
		}
	}
	// 再帰的にルートから階層内のアクティブを再計算する
	RefreshActiveRecursive(world, root, parentActive);
}

void Engine::HierarchySystem::RefreshActiveRecursive(ECSWorld& world, const Entity& entity, bool parentActive) {

	if (!world.IsAlive(entity)) {
		return;
	}

	auto& sceneObject = EnsureSceneObject(world, entity);
	sceneObject.activeInHierarchy = parentActive && sceneObject.activeSelf;
	if (!world.HasComponent<HierarchyComponent>(entity)) {
		return;
	}

	// 子エンティティに対して、再帰的に階層内のアクティブを再計算する
	Entity child = world.GetComponent<HierarchyComponent>(entity).firstChild;
	while (child.IsValid() && world.IsAlive(child)) {

		RefreshActiveRecursive(world, child, sceneObject.activeInHierarchy);
		if (!world.HasComponent<HierarchyComponent>(child)) {
			break;
		}
		child = world.GetComponent<HierarchyComponent>(child).nextSibling;
	}
}

void Engine::HierarchySystem::SetParent(ECSWorld& world, const Entity& child, const Entity& newParent) {

	// 子が存在しないならスキップ
	if (!world.IsAlive(child)) {
		return;
	}

	// 子にHierarchyComponentが無いなら付ける
	if (!world.HasComponent<HierarchyComponent>(child)) {
		world.AddComponent<HierarchyComponent>(child);
	}
	// 子にSceneObjectComponentが無いなら付ける
	if (!world.HasComponent<SceneObjectComponent>(child)) {
		auto& childSceneObject = world.AddComponent<SceneObjectComponent>(child);
		childSceneObject.localFileID = UUID::New();
	}

	// ワールド上で子を新しい親から切り離す
	Detach(world, child);

	auto& hierarchy = world.GetComponent<HierarchyComponent>(child);
	auto& childSceneObject = world.GetComponent<SceneObjectComponent>(child);
	// 新しい親が存在するなら、ワールド上で子を新しい親に付ける
	if (world.IsAlive(newParent)) {

		// 新しい親にHierarchyComponentが無いなら付ける
		if (!world.HasComponent<SceneObjectComponent>(newParent)) {

			auto& parentSceneObject = world.AddComponent<SceneObjectComponent>(newParent);
			parentSceneObject.localFileID = UUID::New();

		}

		// 新しい親のローカルIDを子のHierarchyComponentに設定する
		const auto& parentSceneObject = world.GetComponent<SceneObjectComponent>(newParent);
		hierarchy.parentLocalFileID = parentSceneObject.localFileID;
		if (!childSceneObject.sceneInstanceID) {

			childSceneObject.sceneInstanceID = parentSceneObject.sceneInstanceID;
		}
		AttachLast(world, child, newParent);
	} else {

		hierarchy.parentLocalFileID = UUID{};
	}

	// トランスフォーム変更フラグを立てる
	MarkTransformSubtreeDirty(world, child);
	// 階層内のアクティブをルート以下で再計算する
	RefreshActiveTree(world, child);
}

void Engine::HierarchySystem::Detach(ECSWorld& world, const Entity& child) {

	// 子が存在しない、もしくはHierarchyComponentを持っていないならスキップ
	if (!world.HasComponent<HierarchyComponent>(child)) {
		return;
	}
	auto& childComponent = world.GetComponent<HierarchyComponent>(child);

	Entity parent = childComponent.parent;
	if (!parent.IsValid()) {

		// すでに親なし
		childComponent.parentLocalFileID = UUID{};
		childComponent.prevSibling = Entity::Null();
		childComponent.nextSibling = Entity::Null();
		return;
	}

	auto& parentComponent = world.GetComponent<HierarchyComponent>(parent);

	// 親の子リストの先頭が子なら更新
	if (parentComponent.firstChild == child) {

		parentComponent.firstChild = childComponent.nextSibling;
	}
	// 親の子リストの末尾が子なら更新
	if (parentComponent.lastChild == child) {

		parentComponent.lastChild = childComponent.prevSibling;
	}

	// 兄弟リンク更新
	if (childComponent.prevSibling.IsValid()) {

		world.GetComponent<HierarchyComponent>(childComponent.prevSibling).nextSibling = childComponent.nextSibling;
	}
	if (childComponent.nextSibling.IsValid()) {

		world.GetComponent<HierarchyComponent>(childComponent.nextSibling).prevSibling = childComponent.prevSibling;
	}

	// 子が全部いなくなったら先頭、末尾のエンティティ情報を両方空にする
	if (!parentComponent.firstChild.IsValid()) {

		parentComponent.lastChild = Entity::Null();
	}

	// 子の親子関係を切る
	childComponent.parent = Entity::Null();
	childComponent.prevSibling = Entity::Null();
	childComponent.nextSibling = Entity::Null();
}

void Engine::HierarchySystem::AttachLast(ECSWorld& world, const Entity& child, const Entity& parent) {

	auto& childComponent = world.GetComponent<HierarchyComponent>(child);
	auto& parentComponent = world.GetComponent<HierarchyComponent>(parent);

	// 子に親を設定する
	childComponent.parent = parent;
	childComponent.prevSibling = Entity::Null();
	childComponent.nextSibling = Entity::Null();

	// 子がいないなら先頭、末尾の両方に設定
	if (!parentComponent.firstChild.IsValid()) {

		parentComponent.firstChild = child;
		parentComponent.lastChild = child;
		return;
	}

	Entity last = parentComponent.lastChild;
	if (!last.IsValid()) {

		last = parentComponent.firstChild;
		while (true) {

			auto& lastComponent = world.GetComponent<HierarchyComponent>(last);
			if (!lastComponent.nextSibling.IsValid()) {
				break;
			}
			last = lastComponent.nextSibling;
		}
	}

	auto& lastComponent = world.GetComponent<HierarchyComponent>(last);
	lastComponent.nextSibling = child;
	childComponent.prevSibling = last;
	parentComponent.lastChild = child;
}
