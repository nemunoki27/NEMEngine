#include "EditorCommandContext.h"

//============================================================================
//	EditorCommandContext classMethods
//============================================================================

void Engine::EditorCommandContext::RebuildHierarchyAll() const {

	ECSWorld* world = GetWorld();
	if (!world) {
		return;
	}

	// ワールド内の全てのエンティティをスコープに入れる
	std::vector<Entity> scope;
	scope.reserve(world->GetRecordCount());
	world->ForEachAliveEntity([&](Entity entity) {
		scope.emplace_back(entity);
		});

	// ヒエラルキーを再構築する
	HierarchySystem hierarchySystem;
	hierarchySystem.RebuildRuntimeLinks(*world, scope);
}