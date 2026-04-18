#include "TransformUpdateSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>

//============================================================================
//	TransformUpdateSystem classMethods
//============================================================================

namespace {

	// トランスフォームコンポーネントからローカル行列を作る
	Engine::Matrix4x4 MakeLocalMatrix(const Engine::TransformComponent& transform) {

		return Engine::Matrix4x4::MakeAffineMatrix(transform.localScale,
			transform.localRotation, transform.localPos);
	}
}

void Engine::TransformUpdateSystem::LateUpdate(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	// ルートクリア
	roots_.clear();
	// 親のいないルートを集める
	world.ForEach<TransformComponent, HierarchyComponent>([&](
		Entity entity, TransformComponent&, HierarchyComponent& hierarchy) {
			if (!hierarchy.parent.IsValid() && IsEntityActiveInHierarchy(world, entity)) {
				roots_.emplace_back(entity);
			}
		});
	for (auto root : roots_) {

		auto& rootTransform = world.GetComponent<TransformComponent>(root);

		// 変更があったときのみ更新
		bool rootDirty = rootTransform.isDirty;
		if (rootDirty) {

			rootTransform.worldMatrix = MakeLocalMatrix(rootTransform);
			rootTransform.isDirty = false;
		}
		stack_.clear();
		stack_.push_back({ root, rootDirty });
		while (!stack_.empty()) {

			// スタックから親エンティティを取り出す
			StackNode node = stack_.back();
			stack_.pop_back();

			// 親エンティティ情報
			Entity parent = node.entity;
			bool parentDirty = node.parentDirty;

			const auto& parentHierarchy = world.GetComponent<HierarchyComponent>(parent);
			const Matrix4x4& parentWorld = world.GetComponent<TransformComponent>(parent).worldMatrix;
			// 子エンティティを走査
			Entity child = parentHierarchy.firstChild;
			while (child.IsValid()) {

				auto& childHierarchy = world.GetComponent<HierarchyComponent>(child);
				// 階層内で非アクティブなエンティティはスキップする
				if (!IsEntityActiveInHierarchy(world, child)) {
					child = childHierarchy.nextSibling;
					continue;
				}

				auto& childTransform = world.GetComponent<TransformComponent>(child);

				// 親の変更があったとき、もしくは子自身に変更があったときのみ更新する
				bool childNeedsUpdate = parentDirty || childTransform.isDirty;
				if (childNeedsUpdate) {

					Matrix4x4 childLocal = MakeLocalMatrix(childTransform);
					childTransform.worldMatrix = childLocal * parentWorld;
					childTransform.isDirty = false;
				}
				// 子エンティティをスタックに積む
				stack_.push_back({ child, childNeedsUpdate });
				child = childHierarchy.nextSibling;
			}
		}
	}
}