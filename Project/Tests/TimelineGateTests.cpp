#include "TimelineGateTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelineBuiltinConfig.h>
#include <Engine/Core/Animation/Timeline/TimelineCollisionGate.h>
#include <Engine/Core/Animation/Timeline/TimelinePlaybackState.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/World/Components/Animation/MotionAnimationComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Systems/Animation/TimelineSystem.h>

// c++
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Engine;

namespace {

	void Require(bool condition, const char* message) {

		if (!condition) throw std::runtime_error(message);
	}

	// 同じ判定先を持つ再生状態を作る
	void PrepareGate(ECSWorld& world, Entity target, TimelinePlayerRecord& player,
		TimelinePlaybackState& state, TimelineEvaluationMode mode, uint64_t order, Engine::UUID binding = {}) {

		auto asset = std::make_shared<TimelineAsset>();
		TimelineTrack track;
		track.kind = TimelineTrackKind::Collision;
		track.binding = binding;
		TimelineClip clip;
		clip.data = CreateDefaultTimelineClipData(track.kind);
		track.clips.push_back(clip);
		asset->tracks.push_back(track);
		state.asset = asset;
		state.mode = mode;
		state.order = order;
		if (binding) state.bindings.push_back({binding, "Hitbox", target});
		player.owner = Engine::UUID::New();
		TimelineClipPlayback playback;
		playback.track = &asset->tracks.front();
		playback.clip = &asset->tracks.front().clips.front();
		playback.target = target;
		playback.control = {player.owner, Engine::UUID::New(), player.settings.priority, order};
		state.clips.push_back(std::move(playback));
		InitializeTimelineCollisionGates(world, target, player, state);
	}

	void CheckRuntimeWindow() {

		ECSWorld world;
		const auto target = world.CreateEntity();
		world.AddComponent<CollisionComponent>(target);
		TimelinePlayerRecord player;
		TimelinePlaybackState state;
		PrepareGate(world, target, player, state, TimelineEvaluationMode::Playback, 1);
		Require(!world.GetComponent<CollisionComponent>(target).enabled, "最初の判定区間より前は無効にする");
		auto& playback = state.clips.front();
		SystemContext systems;
		auto context = MakeTimelineEvaluationContext(world, target, state, playback, systems, 0.0, 0.0);
		std::string diagnostic;
		auto instance = CreateBuiltinTimelineClip(*playback.track, *playback.clip, diagnostic);
		Require(instance && instance->Enter(context, diagnostic), "Gateの所有中も判定Clipを開始できる");
		instance->Evaluate(context);
		Require(world.GetComponent<CollisionComponent>(target).enabled, "同じPlayerの判定Clipは有効にできる");
		ApplyTimelineCollisionGates(world, state);
		Require(!world.GetComponent<CollisionComponent>(target).enabled, "次の評価の開始時に区間外へ戻す");
		instance->Evaluate(context);
		instance->Exit(context, TimelineExitReason::Stopped);
		ReleaseTimelineControls(world, playback.control);
		ReleaseTimelineCollisionGates(world, state, false);
		Require(!world.GetComponent<CollisionComponent>(target).enabled, "停止後に判定を残さない");
		Require(!IsTimelineControlled(world, target, "Collision", "enabled"), "停止後に所有権を残さない");
	}

	void CheckSharedPreviewBaseline() {

		ECSWorld world;
		const auto target = world.CreateEntity();
		world.AddComponent<CollisionComponent>(target);
		TimelinePlayerRecord firstPlayer, secondPlayer;
		TimelinePlaybackState first, second;
		PrepareGate(world, target, firstPlayer, first, TimelineEvaluationMode::Preview, 1);
		PrepareGate(world, target, secondPlayer, second, TimelineEvaluationMode::Preview, 2);
		world.GetComponent<CollisionComponent>(target).enabled = true;
		ApplyTimelineCollisionGates(world, first);
		Require(world.GetComponent<CollisionComponent>(target).enabled, "低い優先順位のGateは別Playerを書き換えない");
		ReleaseTimelineCollisionGates(world, first, true);
		Require(world.GetComponent<CollisionComponent>(target).enabled, "別Playerの操作中に復元しない");
		ApplyTimelineCollisionGates(world, second);
		ReleaseTimelineCollisionGates(world, second, true);
		Require(world.GetComponent<CollisionComponent>(target).enabled, "最後のプレビューが最初の共有値へ戻す");
		world.GetComponent<CollisionComponent>(target).enabled = false;
		TimelinePlayerRecord thirdPlayer;
		TimelinePlaybackState third;
		PrepareGate(world, target, thirdPlayer, third, TimelineEvaluationMode::Preview, 3);
		ReleaseTimelineCollisionGates(world, third, true);
		Require(!world.GetComponent<CollisionComponent>(target).enabled, "元から無効な判定は無効へ復元する");
	}

	void CheckExternalOwnerAndRemoval() {

		ECSWorld world;
		const auto target = world.CreateEntity();
		world.AddComponent<CollisionComponent>(target);
		const TimelineControlOwner other{Engine::UUID::New(), Engine::UUID::New(), 10, 1};
		AcquireTimelineControl(world, target, "Collision", "enabled", other);
		TimelinePlayerRecord player;
		TimelinePlaybackState state;
		PrepareGate(world, target, player, state, TimelineEvaluationMode::Playback, 2);
		Require(world.GetComponent<CollisionComponent>(target).enabled, "準備時も上位Playerの判定を無効にしない");
		ReleaseTimelineCollisionGates(world, state, false);
		Require(world.GetComponent<CollisionComponent>(target).enabled, "停止時も上位Playerを上書きしない");
		ReleaseTimelineControls(world, other);
		TimelinePlayerRecord removedPlayer;
		TimelinePlaybackState removed;
		PrepareGate(world, target, removedPlayer, removed, TimelineEvaluationMode::Preview, 3);
		world.DestroyEntity(target);
		ApplyTimelineCollisionGates(world, removed);
		ReleaseTimelineCollisionGates(world, removed, true);
		Require(removed.collisionGates.empty(), "対象削除後も安全に解放する");
	}

	void CheckBindingChange() {

		ECSWorld world;
		const auto first = world.CreateEntity(), second = world.CreateEntity();
		world.AddComponent<CollisionComponent>(first);
		world.AddComponent<CollisionComponent>(second);
		TimelinePlayerRecord player;
		TimelinePlaybackState state;
		PrepareGate(world, first, player, state, TimelineEvaluationMode::Preview, 1, Engine::UUID::New());
		state.bindings.front().entity = second;
		ApplyTimelineCollisionGates(world, state);
		Require(!world.GetComponent<CollisionComponent>(first).enabled &&
			!world.GetComponent<CollisionComponent>(second).enabled, "再生中に割り当てを変更しても区間外は無効にする");
		ReleaseTimelineCollisionGates(world, state, true);
		Require(world.GetComponent<CollisionComponent>(first).enabled &&
			world.GetComponent<CollisionComponent>(second).enabled, "プレビューで変更した割り当て先をすべて復元する");
	}

	struct PreviewLifecycleFixture {

		ECSWorld world;
		TimelineAsset asset = CreateDefaultTimelineAsset("Lifecycle");
		SystemContext context;
		Entity owner{}, mover{}, hitbox{};
		const Vector3 position{2, 3, 4}, scale{1.25f, 0.75f, 2};
		const Quaternion rotation = Quaternion::FromEulerDegrees(Vector3(10, 20, 30));

		PreviewLifecycleFixture() {

			owner = SceneAuthoring::CreateGameObject(world, "Controller");
			mover = SceneAuthoring::CreateGameObject(world, "Mover");
			hitbox = SceneAuthoring::CreateGameObject(world, "Hitbox");
			auto& transform = world.GetComponent<TransformComponent>(mover);
			transform.localPos = position;
			transform.localRotation = rotation;
			transform.localScale = scale;
			world.AddComponent<CollisionComponent>(hitbox);
			asset.bindings.push_back({Engine::UUID::New(), "Hitbox", "Collision"});
			TimelineTrack motion;
			motion.id = Engine::UUID::New();
			motion.kind = TimelineTrackKind::Motion;
			motion.name = "Motion";
			motion.binding = asset.bindings.front().id;
			TimelineClip movement;
			movement.id = Engine::UUID::New();
			movement.duration = 1;
			MotionAnimationSettings settings;
			settings.offset = {4, 0, 0};
			settings.euler = {0, 90, 0};
			settings.applyRotation = true;
			settings.drawDebug = false;
			movement.data = {{"settings", settings}};
			motion.clips.push_back(movement);
			asset.tracks.push_back(motion);
			TimelineTrack collision;
			collision.id = Engine::UUID::New();
			collision.kind = TimelineTrackKind::Collision;
			collision.name = "Collision";
			collision.binding = asset.bindings.back().id;
			TimelineClip interval;
			interval.id = Engine::UUID::New();
			interval.start = 0.25;
			interval.duration = 0.5;
			collision.clips.push_back(interval);
			asset.tracks.push_back(collision);
			context.world = &world;
			context.mode = WorldMode::Edit;
		}

		TimelinePlayerStorage::Handle Preview(double time, bool collision) {

			world.GetComponent<CollisionComponent>(hitbox).enabled = collision;
			world.AddComponent<TimelinePlayerComponent>(owner);
			auto* player = TryGetTimelinePlayer(world, owner);
			player->settings.bindings = {{asset.bindings.front().id, SceneEntityReference::FromEntity(world, mover), world.GetUUID(mover)},
				{asset.bindings.back().id, SceneEntityReference::FromEntity(world, hitbox), world.GetUUID(hitbox)}};
			Require(EvaluateTimelineAt(world, owner, *player, asset, time, context, TimelineEvaluationMode::Preview),
				"実Componentの編集プレビューを開始できる");
			Require((world.GetComponent<TransformComponent>(mover).localPos - position).Length() > 0.01f,
				"編集プレビューで別GameObjectの姿勢を変更する");
			Require(world.GetComponent<CollisionComponent>(hitbox).enabled == (time >= 0.25 && time <= 0.75),
				"編集プレビューで判定区間の内外を反映する");
			return world.GetComponent<TimelinePlayerComponent>(owner).handle;
		}

		void CheckRestored(bool collision) {

			const auto& transform = world.GetComponent<TransformComponent>(mover);
			Require((transform.localPos - position).Length() < 1e-5f && (transform.localScale - scale).Length() < 1e-5f &&
				std::abs(transform.localRotation.x - rotation.x) < 1e-5f && std::abs(transform.localRotation.y - rotation.y) < 1e-5f &&
				std::abs(transform.localRotation.z - rotation.z) < 1e-5f && std::abs(transform.localRotation.w - rotation.w) < 1e-5f,
				"終了時に元の位置・回転・scaleを復元する");
			Require(world.GetComponent<CollisionComponent>(hitbox).enabled == collision, "終了時にCollisionの元値を復元する");
			Require(!IsTimelineControlled(world, mover, "Transform") && !IsTimelineControlled(world, hitbox, "Collision"),
				"終了時に別対象への所有権を残さない");
		}
	};

	// コンポーネント削除から実Storageの解放とプレビュー復元を通す
	void CheckPreviewComponentRemoval() {

		PreviewLifecycleFixture fixture;
		const auto handle = fixture.Preview(0.5, false);
		fixture.world.RemoveComponent<TimelinePlayerComponent>(fixture.owner);
		fixture.CheckRestored(false);
		Require(!fixture.world.GetStorage().Get<TimelinePlayerStorage>().TryGet(handle), "Component削除で再生Storageを解放する");
		fixture.Preview(0.1, true);
		fixture.world.RemoveComponent<TimelinePlayerComponent>(fixture.owner);
		fixture.CheckRestored(true);
	}

	// 再生元の破棄後も残っている割り当て先を復元する
	void CheckPreviewOwnerDestruction() {

		PreviewLifecycleFixture fixture;
		const auto handle = fixture.Preview(0.5, false);
		fixture.world.DestroyEntity(fixture.owner);
		fixture.world.FlushPendingDestroyEntities();
		TimelineSystem system;
		system.Update(fixture.world, fixture.context);
		Require(!fixture.world.IsAlive(fixture.owner), "再生元を実際に破棄する");
		fixture.CheckRestored(false);
		Require(!fixture.world.GetStorage().Get<TimelinePlayerStorage>().TryGet(handle), "再生元破棄でStorageを解放する");
	}

	// World終了と重複した終了通知のどちらでも元値を保つ
	void CheckPreviewWorldExit() {

		PreviewLifecycleFixture fixture;
		fixture.Preview(0.5, false);
		TimelineSystem system;
		system.OnWorldExit(fixture.world, fixture.context);
		fixture.CheckRestored(false);
		const auto* player = TryGetTimelinePlayer(fixture.world, fixture.owner);
		Require(player && !player->runtime.playback, "World終了で再生状態を解放する");
		system.OnWorldExit(fixture.world, fixture.context);
		fixture.CheckRestored(false);
	}
}

bool NEMTests::TestTimelineCollisionGates() {

	try {
		RegisterBuiltinAnimationProperties();
		CheckRuntimeWindow();
		CheckSharedPreviewBaseline();
		CheckExternalOwnerAndRemoval();
		CheckBindingChange();
		CheckPreviewComponentRemoval();
		CheckPreviewOwnerDestruction();
		CheckPreviewWorldExit();
		return true;
	} catch (const std::exception& error) {
		std::cerr << "[TimelineCollisionGate] " << error.what() << '\n';
		return false;
	}
}
