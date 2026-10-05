#include "EditorEntityPreview.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Entity/EditorEntitySnapshot.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

//============================================================================
//	EditorEntityPreview classMethods
//============================================================================
Engine::EditorEntityPreview::~EditorEntityPreview() {

	try {
		End();
	} catch (...) {
		// 終了処理から例外を出さない
		try {
			Logger::Output(LogType::Engine, spdlog::level::err, "仮Entityの破棄に失敗しました");
		} catch (...) {
		}
	}
}

bool Engine::EditorEntityPreview::Begin(ECSWorld& world, Entity entity) {

	if (!world.IsAlive(entity)) {
		return false;
	}
	if (BelongsTo(world) && entity_ == entity) {
		return true;
	}

	// 前のプレビューを片付けて所有を受け取る
	const auto lifetime = world.GetLifetime();
	End();
	if (!lifetime->IsAlive() || !world.IsAlive(entity)) {
		return false;
	}
	world_ = &world;
	lifetime_ = lifetime;
	entity_ = entity;
	return true;
}

void Engine::EditorEntityPreview::End() {

	ECSWorld* world = world_;
	const auto lifetime = lifetime_.lock();
	const Entity entity = entity_;

	// 破棄通知が再入しても同じEntityを消さない
	Reset();
	if (world && lifetime && lifetime->IsAlive() && world->IsAlive(entity)) {
		EditorEntitySnapshotUtility::DestroySubtree(*world, entity);
	}
}

Engine::Entity Engine::EditorEntityPreview::Release() {

	// 生存するEntityの所有だけを呼出元へ渡す
	const Entity entity = GetEntity();
	Reset();
	return entity;
}

bool Engine::EditorEntityPreview::BelongsTo(const ECSWorld& world) const {

	const auto lifetime = lifetime_.lock();
	return lifetime && lifetime->IsAlive() && world_ == &world && lifetime == world.GetLifetime();
}

Engine::Entity Engine::EditorEntityPreview::GetEntity() const {

	// Worldの寿命を確認してからEntityを参照
	const auto lifetime = lifetime_.lock();
	return world_ && lifetime && lifetime->IsAlive() && world_->IsAlive(entity_) ? entity_ : Entity::Null();
}

bool Engine::EditorEntityPreview::IsActive() const {

	return GetEntity().IsValid();
}

void Engine::EditorEntityPreview::Reset() {

	world_ = nullptr;
	lifetime_.reset();
	entity_ = Entity::Null();
}
