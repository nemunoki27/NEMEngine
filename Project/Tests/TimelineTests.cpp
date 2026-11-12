#include "TimelineTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelinePlayback.h>
#include <Engine/Core/Animation/Timeline/TimelinePlayerAccess.h>
#include <Engine/Core/Animation/Timeline/TimelineBuiltinConfig.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Assets/Database/AssetDependencyScanner.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

// c++
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Engine;

namespace {

	void Require(bool value, const char* message) {

		if (!value) throw std::runtime_error(message);
	}

	TimelineTrack MakeTrack(TimelineTrackKind kind, Engine::UUID binding = {}) {

		TimelineTrack track;
		track.id = Engine::UUID::New();
		track.kind = kind;
		track.name = std::string(GetTimelineTrackKindName(kind));
		track.binding = binding;
		return track;
	}

	TimelineClip MakeClip(double start, double duration, std::string name) {

		TimelineClip clip;
		clip.id = Engine::UUID::New();
		clip.start = start;
		clip.duration = duration;
		clip.name = std::move(name);
		return clip;
	}

	// 描画周期が変わっても通知の通過回数をそろえる
	void CheckClock(double delta) {

		ECSWorld world;
		const auto actor = world.CreateEntity();
		auto asset = CreateDefaultTimelineAsset("clock");
		asset.durationMode = TimelineDurationMode::FixedLength;
		asset.duration = 1.0;
		auto signals = MakeTrack(TimelineTrackKind::Event);
		for (double time : {0.0, 0.1, 0.125, 0.9}) signals.clips.push_back(MakeClip(time, 0.0, "signal"));
		asset.tracks.push_back(signals);
		auto windows = MakeTrack(TimelineTrackKind::Window);
		windows.clips.push_back(MakeClip(0.2, 0.3, "Combo"));
		asset.tracks.push_back(windows);
		TimelinePlayerRecord player;
		player.runtime.playing = true;
		SystemContext context;
		context.mode = WorldMode::Play;
		for (int i = 0; i < static_cast<int>(std::ceil(1.0 / delta)); ++i)
			AdvanceTimelinePlayer(world, actor, player, asset, delta, context);
		Require(player.runtime.notifications.size() == 4, "描画周期によってイベントを失ったり重複しない");
		Require(player.runtime.finished && std::abs(player.runtime.time - 1.0) < 1e-8, "終了時刻を正確に確定する");
		Require(player.runtime.windows.empty(), "完了後に受付区間を残さない");
		EvaluateTimelineAt(world, actor, player, asset, 0.35, context, TimelineEvaluationMode::Seek, false);
		Require(player.runtime.windows.contains("Combo") && player.runtime.notifications.size() == 4,
			"Seekは受付状態だけを評価し通知を再発火しない");
		ReleaseTimelinePlayer(world, actor, player, context);
	}

	// 対象の有無と距離条件を開始時点で確定する
	void CheckBranches() {

		ECSWorld world;
		const auto actor = world.CreateEntity();
		const auto enemy = world.CreateEntity();
		world.AddComponent<TransformComponent>(actor);
		world.AddComponent<TransformComponent>(enemy);
		world.GetComponent<TransformComponent>(enemy).localPos = {2, 0, 0};
		auto asset = CreateDefaultTimelineAsset("branches");
		const auto actorSlot = asset.bindings.front().id;
		const auto targetSlot = Engine::UUID::New();
		asset.bindings.push_back({targetSlot, "Target", "Transform"});
		const auto branch = Engine::UUID::New();
		auto nearTrack = MakeTrack(TimelineTrackKind::Group);
		nearTrack.branchGroup = branch;
		TimelineCondition condition;
		condition.kind = TimelineConditionKind::Distance;
		condition.firstBinding = actorSlot;
		condition.secondBinding = targetSlot;
		condition.comparison = TimelineComparison::LessEqual;
		condition.distance = 3;
		nearTrack.conditions.push_back(condition);
		auto farTrack = MakeTrack(TimelineTrackKind::Group);
		farTrack.branchGroup = branch;
		farTrack.fallback = true;
		auto nearWindow = MakeTrack(TimelineTrackKind::Window);
		nearWindow.parent = nearTrack.id;
		nearWindow.clips.push_back(MakeClip(0, 1, "Near"));
		auto farWindow = MakeTrack(TimelineTrackKind::Window);
		farWindow.parent = farTrack.id;
		farWindow.clips.push_back(MakeClip(0, 1, "Far"));
		asset.tracks = {nearTrack, farTrack, nearWindow, farWindow};
		TimelinePlayerRecord player;
		player.settings.bindings.push_back({targetSlot, {}, world.GetUUID(enemy)});
		SystemContext context;
		EvaluateTimelineAt(world, actor, player, asset, 0.25, context, TimelineEvaluationMode::Preview);
		Require(player.runtime.windows.contains("Near") && !player.runtime.windows.contains("Far"), "近距離の分岐を選択する");
		world.GetComponent<TransformComponent>(enemy).localPos = {10, 0, 0};
		EvaluateTimelineAt(world, actor, player, asset, 0.5, context, TimelineEvaluationMode::Preview);
		Require(player.runtime.windows.contains("Near"), "グループ進入後は分岐が揺れない");
		ReleaseTimelinePlayer(world, actor, player, context);
		world.GetComponent<TransformComponent>(enemy).localPos = {10, 0, 0};
		EvaluateTimelineAt(world, actor, player, asset, 0.25, context, TimelineEvaluationMode::Preview);
		Require(player.runtime.windows.contains("Far"), "再評価で遠距離の分岐を選択する");
		ReleaseTimelinePlayer(world, actor, player, context);
		world.DestroyEntity(enemy);
		EvaluateTimelineAt(world, actor, player, asset, 0.25, context, TimelineEvaluationMode::Preview);
		Require(player.runtime.windows.contains("Far"), "対象が消えても距離ゼロと誤判定しない");
		ReleaseTimelinePlayer(world, actor, player, context);
	}

	// 同じプロパティへの競合とグローバルな時間倍率を区別する
	void CheckOwnership() {

		ECSWorld world;
		const auto first = world.CreateEntity(), second = world.CreateEntity();
		TimelineControlOwner lower{Engine::UUID::New(), Engine::UUID::New(), 0, 1};
		TimelineControlOwner higher{Engine::UUID::New(), Engine::UUID::New(), 1, 2};
		Require(AcquireTimelineControl(world, first, "Transform", "localPos", lower), "最初の所有者を登録する");
		AcquireTimelineControl(world, first, "Transform", "localPos", higher);
		Require(!OwnsTimelineControl(world, first, "Transform", "localPos", lower), "優先度で所有者を選択する");
		ReleaseTimelineControls(world, higher);
		Require(OwnsTimelineControl(world, first, "Transform", "localPos", lower), "残っている所有者へ返す");
		AcquireTimelineControl(world, first, "GlobalTime", "timeScale", lower);
		AcquireTimelineControl(world, second, "GlobalTime", "timeScale", higher);
		Require(!OwnsTimelineControl(world, first, "GlobalTime", "timeScale", lower), "全体時計の制御先はEntityに依存しない");
		ReleaseTimelinePlayerControls(world, lower.player);
		ReleaseTimelinePlayerControls(world, higher.player);
		Require(!IsTimelineControlled(world, first, "Transform"), "制御権を残さない");
	}
}

bool NEMTests::TestTimeline() {

	try {
		RegisterBuiltinAnimationProperties();
		auto asset = CreateDefaultTimelineAsset("roundtrip");
		asset.parameters.push_back({Engine::UUID::New(), "Speed", 1.25f});
		const auto saved = nlohmann::json(asset);
		Require(nlohmann::json(saved.get<TimelineAsset>()) == saved, "Timelineを往復保存する");
		Require(AssetTypeResolver::GuessByPath("Attack.timeline.json") == AssetType::Timeline, "複合拡張子を識別する");
		AssetDependencyScanner::IDReferences references;
		AssetDependencyScanner::PathReferences paths;
		const auto timelineID = ParseAssetID(nlohmann::json{{"timeline", "00000000000000000000000000000001"}}, "timeline");
		const auto prefabID = ParseAssetID(nlohmann::json{{"asset", "00000000000000000000000000000002"}}, "asset");
		const nlohmann::json dependencies = {{"timeline", ToAssetReferenceJson(timelineID)},
			{"kind", "Prefab"}, {"clips", {{{"data", {{"asset", ToAssetReferenceJson(prefabID)}}}}}}};
		AssetDependencyScanner::ScanReferences(dependencies, references, paths);
		Require(references.contains(timelineID) && references.find(timelineID)->second == AssetType::Timeline &&
			references.contains(prefabID) && references.find(prefabID)->second == AssetType::Prefab, "Timelineと生成Prefabを依存先へ含める");
		auto group = MakeTrack(TimelineTrackKind::Group);
		group.parent = group.id;
		asset.tracks.push_back(group);
		std::string diagnostic;
		Require(!ValidateTimelineAsset(asset, diagnostic), "グループの循環参照を拒否する");
		CheckClock(1.0 / 30.0);
		CheckClock(1.0 / 60.0);
		CheckClock(1.0 / 120.0);
		CheckBranches();
		CheckOwnership();
		return true;
	} catch (const std::exception& error) {
		std::cerr << "Timeline: " << error.what() << '\n';
		return false;
	}
}
