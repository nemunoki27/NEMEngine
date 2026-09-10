#include "HierarchySystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>

// c++
#include <algorithm>

//============================================================================
//	HierarchySystem classMethods
//============================================================================

void Engine::HierarchySystem::OnWorldEnter(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	// ワールド内の全生存エンティティをスコープにして親子関係を再構築
	std::vector<Entity> scope;
	scope.reserve(world.GetRecordCount());
	world.ForEachAliveEntity([&](Entity entity) {
		scope.emplace_back(entity);
		});
	// UUIDから実行時の親子リンクを構築
	RebuildRuntimeLinks(world, scope);
}

void Engine::HierarchySystem::RebuildRuntimeLinks(ECSWorld& world, const std::vector<Entity>& scope) {

	// ローカルIDとシーンインスタンスIDの組み合わせをキーにしてエンティティを高速検索するためのマップ
	struct LocalKey {
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

	std::unordered_map<LocalKey, Entity, LocalKeyHash> entityMap;
	entityMap.reserve(scope.size());

	// 各エンティティの親子リンクを初期化し、IDマップへ登録
	for (const auto& entity : scope) {

		if (!world.IsAlive(entity) || !world.HasComponent<HierarchyComponent>(entity)) {
			continue;
		}

		auto& hierarchy = world.GetComponent<HierarchyComponent>(entity);
		hierarchy.parent = Entity::Null();
		hierarchy.firstChild = Entity::Null();
		hierarchy.lastChild = Entity::Null();
		hierarchy.nextSibling = Entity::Null();
		hierarchy.prevSibling = Entity::Null();

		if (world.HasComponent<SceneObjectComponent>(entity)) {
			const auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
			if (sceneObject.localFileID) {
				entityMap[{ sceneObject.sceneInstanceID, sceneObject.localFileID }] = entity;
			}
		}
	}

	// 保存されていた親IDから実際のEntityポインタを解決してリンクを繋ぐ
	for (const auto& entity : scope) {

		if (!world.IsAlive(entity) || !world.HasComponent<HierarchyComponent>(entity)) {
			continue;
		}
		if (!world.HasComponent<SceneObjectComponent>(entity)) {
			continue;
		}

		const UUID parentLocalFileID = world.GetComponent<HierarchyComponent>(entity).parentLocalFileID;
		if (!parentLocalFileID) {
			continue;
		}

		const auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
		auto it = entityMap.find({ sceneObject.sceneInstanceID, parentLocalFileID });
		if (it == entityMap.end()) {
			continue;
		}

		Entity parent = it->second;
		if (world.IsAlive(parent)) {
			// 通常の親子関係を優先し、同一実体がジョイント側にも表示される状態を防ぐ
			if (world.HasComponent<JointAttachmentComponent>(entity)) {
				world.RemoveComponent<JointAttachmentComponent>(entity);
			}
			if (!world.HasComponent<HierarchyComponent>(parent)) {
				world.AddComponent<HierarchyComponent>(parent);
			}
			AttachLast(world, entity, parent);
		}
	}

	// 兄弟順に基づいて子リンクをソートし、正しい順序を復元
	for (const auto& entity : scope) {

		HierarchyUtility::SortChildLinksBySiblingOrder(world, entity);
	}

	// 階層全体のアクティブ状態を親子連動で再計算
	RefreshAllActiveStates(world, scope);
}

void Engine::HierarchySystem::RefreshAllActiveStates(ECSWorld& world, const std::vector<Entity>& scope) {

	for (const auto& entity : scope) {

		if (!world.IsAlive(entity)) {
			continue;
		}
		// 親なしのルートエンティティから順に子孫へアクティブ状態を伝播させる
		if (HierarchyUtility::IsRoot(world, entity)) {
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
			// 親がいればそのアクティブ状態を引き継ぐ
			parentActive = SceneObjectUtility::EnsureSceneObject(world, hierarchy.parent).activeInHierarchy;
		}
	}
	if (RefreshActiveRecursive(world, root, parentActive)) {
		// 再有効化された部分木は停止中に保持したlocal値からworldMatrixを再構築する
		MarkTransformSubtreeDirty(world, root);
	}
}

bool Engine::HierarchySystem::RefreshActiveRecursive(ECSWorld& world, const Entity& entity, bool parentActive) {

	if (!world.IsAlive(entity)) {
		return false;
	}

	// 自身のアクティブ状態＝親がアクティブかつ自身が有効設定
	auto& sceneObject = SceneObjectUtility::EnsureSceneObject(world, entity);
	const bool activeInHierarchy = parentActive && sceneObject.activeSelf;
	bool activated = false;
	if (sceneObject.activeInHierarchy != activeInHierarchy) {
		activated = activeInHierarchy;
		sceneObject.activeInHierarchy = activeInHierarchy;
		world.MarkComponentModified<SceneObjectComponent>(entity);
	}
	
	if (!world.HasComponent<HierarchyComponent>(entity)) {
		return activated;
	}

	// 子に対しても再帰的に適用する、階層が深い場合はスタックオーバーフローに注意が必要だが通常は許容範囲
	Entity child = world.GetComponent<HierarchyComponent>(entity).firstChild;
	while (child.IsValid() && world.IsAlive(child)) {

		activated |= RefreshActiveRecursive(
			world, child, sceneObject.activeInHierarchy);
		if (!world.HasComponent<HierarchyComponent>(child)) {
			break;
		}
		child = world.GetComponent<HierarchyComponent>(child).nextSibling;
	}
	return activated;
}

void Engine::HierarchySystem::UpdateActiveInHierarchy(ECSWorld& world, const Entity& entity) {

	// 親のアクティブ状態を引き継いで、指定エンティティ以下を再計算する
	RefreshActiveTree(world, entity);
}

void Engine::HierarchySystem::SetParent(ECSWorld& world, const Entity& child, const Entity& newParent) {

	if (!world.IsAlive(child)) {
		return;
	}
	if (world.IsAlive(newParent)) {

		Entity ancestor = newParent;
		size_t remaining = world.GetRecordCount() + 1;
		while (world.IsAlive(ancestor) && remaining-- > 0) {

			if (ancestor == child) {
				return;
			}
			const HierarchyComponent* hierarchy = world.TryGetComponent<HierarchyComponent>(ancestor);
			ancestor = hierarchy ? hierarchy->parent : Entity::Null();
		}
	}

	// 必要なコンポーネントの確保
	if (!world.HasComponent<HierarchyComponent>(child)) {
		world.AddComponent<HierarchyComponent>(child);
	}
	SceneObjectUtility::EnsureSceneObject(world, child);
	// 生成予約中の親にも階層を確保し、参照取得前に構造変更を終える
	if (world.IsAlive(newParent)) {
		if (!world.HasComponent<HierarchyComponent>(newParent)) {
			world.AddComponent<HierarchyComponent>(newParent);
		}
		SceneObjectUtility::EnsureSceneObject(world, newParent);
	}

	// 現在の親から切り離し
	Detach(world, child);
	// 通常の親を設定する場合はジョイント親子付けを解除する
	if (world.IsAlive(newParent) && world.HasComponent<JointAttachmentComponent>(child)) {
		world.RemoveComponent<JointAttachmentComponent>(child);
	}

	auto& hierarchy = world.GetComponent<HierarchyComponent>(child);
	auto& childSceneObject = world.GetComponent<SceneObjectComponent>(child);
	
	// 新しい親へのアタッチ
	if (world.IsAlive(newParent)) {

		const auto& parentSceneObject = world.GetComponent<SceneObjectComponent>(newParent);
		hierarchy.parentLocalFileID = parentSceneObject.localFileID;
		// シーンインスタンスIDの継承で基本的には親と同じシーンに属するようにする
		if (!childSceneObject.sceneInstanceID) {
			childSceneObject.sceneInstanceID = parentSceneObject.sceneInstanceID;
		}
		AttachLast(world, child, newParent);
	} else {
		// 親なしのルートへ
		hierarchy.parentLocalFileID = UUID{};
	}

	// 親子関係が変わったため、自身と子孫のワールド行列を再計算対象にする
	MarkTransformSubtreeDirty(world, child);
	// アクティブ状態も再評価
	RefreshActiveTree(world, child);
}

void Engine::HierarchySystem::Detach(ECSWorld& world, const Entity& child) {

	if (!world.HasComponent<HierarchyComponent>(child)) {
		return;
	}
	auto& childComponent = world.GetComponent<HierarchyComponent>(child);

	Entity parent = childComponent.parent;
	if (!world.IsAlive(parent) || !world.HasComponent<HierarchyComponent>(parent)) {
		// すでにルート
		childComponent.parent = Entity::Null();
		childComponent.parentLocalFileID = UUID{};
		childComponent.prevSibling = Entity::Null();
		childComponent.nextSibling = Entity::Null();
		return;
	}

	auto& parentComponent = world.GetComponent<HierarchyComponent>(parent);

	// 親の子リストの前後関係を繋ぎ替える
	if (parentComponent.firstChild == child) {
		parentComponent.firstChild = childComponent.nextSibling;
	}
	if (parentComponent.lastChild == child) {
		parentComponent.lastChild = childComponent.prevSibling;
	}

	// 兄弟間のリンクを繋ぎ替える
	if (world.IsAlive(childComponent.prevSibling) &&
		world.HasComponent<HierarchyComponent>(childComponent.prevSibling)) {
		world.GetComponent<HierarchyComponent>(childComponent.prevSibling).nextSibling = childComponent.nextSibling;
	}
	if (world.IsAlive(childComponent.nextSibling) &&
		world.HasComponent<HierarchyComponent>(childComponent.nextSibling)) {
		world.GetComponent<HierarchyComponent>(childComponent.nextSibling).prevSibling = childComponent.prevSibling;
	}

	if (!parentComponent.firstChild.IsValid()) {
		parentComponent.lastChild = Entity::Null();
	}

	// 親子リンクを切断
	childComponent.parent = Entity::Null();
	childComponent.prevSibling = Entity::Null();
	childComponent.nextSibling = Entity::Null();
}

void Engine::HierarchySystem::AttachLast(ECSWorld& world, const Entity& child, const Entity& parent) {

	auto& childComponent = world.GetComponent<HierarchyComponent>(child);
	auto& parentComponent = world.GetComponent<HierarchyComponent>(parent);

	childComponent.parent = parent;
	childComponent.prevSibling = Entity::Null();
	childComponent.nextSibling = Entity::Null();
	if (!world.IsAlive(parentComponent.firstChild) ||
		!world.HasComponent<HierarchyComponent>(parentComponent.firstChild)) {

		parentComponent.firstChild = Entity::Null();
		parentComponent.lastChild = Entity::Null();
	}

	// 親に子が一人もいない場合は先頭かつ末尾として登録
	if (!parentComponent.firstChild.IsValid()) {
		parentComponent.firstChild = child;
		parentComponent.lastChild = child;
		return;
	}

	// 親の子リストの末尾に追加する、lastChildがキャッシュされていれば高速でなければ辿る
	Entity last = parentComponent.lastChild;
	if (!world.IsAlive(last) || !world.HasComponent<HierarchyComponent>(last)) {
		last = parentComponent.firstChild;
		while (world.IsAlive(last) && world.HasComponent<HierarchyComponent>(last)) {
			auto& lastComponent = world.GetComponent<HierarchyComponent>(last);
			if (!world.IsAlive(lastComponent.nextSibling) ||
				!world.HasComponent<HierarchyComponent>(lastComponent.nextSibling)) {
				break;
			}
			last = lastComponent.nextSibling;
		}
	}
	if (!world.IsAlive(last) || !world.HasComponent<HierarchyComponent>(last)) {

		parentComponent.firstChild = child;
		parentComponent.lastChild = child;
		return;
	}

	auto& lastComponent = world.GetComponent<HierarchyComponent>(last);
	lastComponent.nextSibling = child;
	childComponent.prevSibling = last;
	parentComponent.lastChild = child;
}
