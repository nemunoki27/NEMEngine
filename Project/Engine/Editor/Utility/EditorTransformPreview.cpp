#include "EditorTransformPreview.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/CompositeEditorCommand.h>
#include <Engine/Editor/Commands/Transform/SetTransformCommand.h>
#include <Engine/Editor/Commands/Transform/TransformEditUtility.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>

//============================================================================
//	EditorTransformPreview classMethods
//============================================================================
Engine::EditorTransformPreview::~EditorTransformPreview() {

	try {
		Cancel();
	} catch (...) {
		// 終了処理から例外を出さない
		try {
			Logger::Output(LogType::Engine, spdlog::level::err, "姿勢プレビューの取消に失敗しました");
		} catch (...) {
		}
	}
}

bool Engine::EditorTransformPreview::Begin(ECSWorld& world, std::span<const Entity> targets, bool runtimeOnly) {

	// 全対象を検証してから前の編集を終了する
	std::vector<std::pair<UUID, TransformComponent>> snapshots;
	for (Entity entity : targets) {
		if (!world.IsAlive(entity) || !world.HasComponent<TransformComponent>(entity)) {
			return false;
		}
		const UUID uuid = world.GetUUID(entity);
		if (std::ranges::none_of(snapshots, [&](const auto& item) { return item.first == uuid; })) {
			snapshots.emplace_back(uuid, world.GetComponent<TransformComponent>(entity));
		}
	}
	if (snapshots.empty()) {
		return false;
	}

	const auto lifetime = world.GetLifetime();
	Cancel();
	if (!lifetime->IsAlive()) {
		return false;
	}
	// 前の取消後の姿勢を開始値にする
	for (auto& [uuid, transform] : snapshots) {
		const Entity entity = world.FindByUUID(uuid);
		if (!world.IsAlive(entity) || !world.HasComponent<TransformComponent>(entity)) {
			return false;
		}
		transform = world.GetComponent<TransformComponent>(entity);
	}
	world_ = &world;
	lifetime_ = lifetime;
	snapshots_ = std::move(snapshots);
	runtimeOnly_ = runtimeOnly;
	return true;
}

void Engine::EditorTransformPreview::Cancel() {

	ECSWorld* world = world_;
	const auto lifetime = lifetime_.lock();
	const bool restore = !runtimeOnly_;
	auto snapshots = std::move(snapshots_);
	// 変更通知が再入しても同じ編集を戻さない
	Release();
	if (!restore || !world || !lifetime || !lifetime->IsAlive()) {
		return;
	}
	for (const auto& [uuid, transform] : snapshots) {
		// 通知中のWorld破棄も確認する
		if (!lifetime->IsAlive()) {
			break;
		}
		TransformEditUtility::ApplyImmediate(*world, world->FindByUUID(uuid), transform);
	}
}

void Engine::EditorTransformPreview::Release() {

	world_ = nullptr;
	lifetime_.reset();
	snapshots_.clear();
	runtimeOnly_ = false;
}

std::unique_ptr<Engine::IEditorCommand> Engine::EditorTransformPreview::BuildCommand(const ECSWorld& world) const {

	if (runtimeOnly_ || !BelongsTo(world)) {
		return {};
	}
	// 開始時の対象だけを一括編集へ渡す
	std::vector<std::unique_ptr<IEditorCommand>> commands;
	for (const auto& [uuid, before] : snapshots_) {
		const Entity entity = world.FindByUUID(uuid);
		const auto* after = world.TryGetComponent<TransformComponent>(entity);
		if (!after) {
			continue;
		}
		if (!SetTransformCommand::NearlyEqualTransform(before, *after)) {
			commands.emplace_back(std::make_unique<SetTransformCommand>(entity, before, *after));
		}
	}
	return commands.empty() ? nullptr : std::make_unique<CompositeEditorCommand>(std::move(commands), true);
}

bool Engine::EditorTransformPreview::BelongsTo(const ECSWorld& world) const {

	const auto lifetime = lifetime_.lock();
	return lifetime && lifetime->IsAlive() && world_ == &world && lifetime == world.GetLifetime();
}
