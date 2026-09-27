#include "SceneCreationScope.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <exception>
#include <stdexcept>

//============================================================================
//	SceneCreationScope classMethods
//============================================================================
Engine::SceneCreationScope::SceneCreationScope(ECSWorld& world) : world_(world), creation_(world) {}

Engine::SceneCreationScope::~SceneCreationScope() {

	if (committed_ || !creation_.HasCreations()) {
		return;
	}
	std::exception_ptr failure;
	try {
		creation_.Rollback();
	} catch (...) {
		failure = std::current_exception();
	}
	try {

		// 消した生成物を親子リンクから取り除く
		std::vector<Entity> remaining;
		world_.ForEachAliveEntity([&](Entity entity) { remaining.emplace_back(entity); });
		HierarchySystem hierarchy;
		hierarchy.RebuildRuntimeLinks(world_, remaining);
	} catch (...) {
		failure = std::current_exception();
	}
	if (failure) {
		try {
			Logger::Output(LogType::Engine, spdlog::level::err, "Scene生成の取消処理に失敗しました");
		} catch (...) {
			// 終了処理から診断の例外を出さない
		}
	}
}

void Engine::SceneCreationScope::Commit() {

	creation_.Commit();
	committed_ = true;
}

void Engine::SceneCreationScope::DestroyCreated(const Entity& entity) {

	if (!creation_.Contains(entity)) {
		throw std::invalid_argument("生成範囲外のEntityは取消できません");
	}
	// 生存する親と兄弟から先に切り離す
	HierarchySystem hierarchy;
	hierarchy.SetParent(world_, entity, Entity::Null());
	creation_.DestroyCreated(entity);
}
