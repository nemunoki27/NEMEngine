#include "ECSCreationScope.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <exception>
#include <stdexcept>

//============================================================================
//	ECSCreationScope classMethods
//============================================================================
Engine::ECSCreationScope::ECSCreationScope(ECSWorld& world) : world_(world), lifetime_(world.GetLifetime()) {

	if (world.IsStructuralChangeDeferred()) {
		throw std::logic_error("走査中のEntity生成はCommandへ予約してください");
	}
	listenerID_ = world_.AddComponentMutationListener(&OnMutation, this);
}

Engine::ECSCreationScope::~ECSCreationScope() {

	try {
		Rollback();
	} catch (...) {
		try {
			Logger::Output(LogType::Engine, spdlog::level::err, "Entity生成の取消通知に失敗しました");
		} catch (...) {
			// 終了処理から診断の例外を出さない
		}
	}
}

void Engine::ECSCreationScope::Commit() {

	// 外側の生成範囲は引き続き同じEntityを保持する
	EndRegistration();
	created_.clear();
}

void Engine::ECSCreationScope::Rollback() {

	EndRegistration();
	std::exception_ptr failure;
	while (lifetime_->IsAlive() && !created_.empty()) {

		// 通知が失敗しても残りの生成物を回収する
		const Entity entity = created_.back();
		created_.pop_back();
		try {
			world_.DestroyEntityImmediate(entity);
		} catch (...) {
			if (!failure) {
				failure = std::current_exception();
			}
		}
	}
	// World終了で回収済みの記録を残さない
	created_.clear();
	if (failure) {
		std::rethrow_exception(failure);
	}
}

void Engine::ECSCreationScope::DestroyCreated(const Entity& entity) {

	lifetime_->ThrowIfEnded();
	// 別の操作で予約された削除には触れない
	if (!Contains(entity)) {
		throw std::invalid_argument("生成範囲外のEntityは取消できません");
	}
	world_.DestroyEntityImmediate(entity);
}

bool Engine::ECSCreationScope::Contains(const Entity& entity) const {

	return std::find(created_.begin(), created_.end(), entity) != created_.end();
}

void Engine::ECSCreationScope::EndRegistration() {

	if (lifetime_->IsAlive()) {
		world_.RemoveComponentMutationListener(listenerID_);
	}
	listenerID_ = 0;
}

void Engine::ECSCreationScope::OnMutation([[maybe_unused]] ECSWorld& world, const Entity& entity,
	[[maybe_unused]] uint32_t typeID, ComponentMutationKind kind, void* userData) {

	if (kind == ComponentMutationKind::EntityCreated) {
		auto& scope = *static_cast<ECSCreationScope*>(userData);
		scope.created_.emplace_back(entity);
	}
}
