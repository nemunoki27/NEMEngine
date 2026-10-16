#include "AnimationPlaybackTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Systems/Animation/AnimationPlayerSystem.h>
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

bool NEMTests::TestAnimationPlayback() {

	using namespace Engine;
	RegisterBuiltinAnimationProperties();
	TestDirectory directory("AnimationPlayback", RuntimePaths::GetGameAssetsRoot());
	const auto path = directory.GetPath() / "Move.animclip.json";
	AnimationClipAsset clip;
	clip.duration = 1.0f;
	clip.autoDuration = false;
	AnimationCurveTrack track;
	track.binding = { "Transform", "localPos", AnimationValueType::Vector3 };
	track.channels = MakeDefaultAnimationChannels(AnimationValueType::Vector3);
	track.channels[0].AddKey(0.0f, 0.0f);
	track.channels[0].AddKey(1.0f, 10.0f);
	clip.curveTracks.push_back(track);
	if (!SaveAnimationClipAsset(path, clip)) return false;
	AssetDatabase database;
	database.Init();
	if (!database.RebuildMeta({directory.GetPath()})) return false;
	const auto* meta = database.FindByPath(RuntimePaths::ToAssetPath(path));
	if (!meta) return false;
	AnimationClipManager clips;
	SystemContext context;
	context.assetDatabase = &database;
	context.animationClipManager = &clips;
	context.mode = WorldMode::Play;
	context.deltaTime = 0.25f;
	ECSWorld world;
	const Entity entity = world.CreateEntity();
	world.AddComponent<TransformComponent>(entity);
	world.AddComponent<AnimationPlayerComponent>(entity);
	auto& player = world.GetComponent<AnimationPlayerComponent>(entity);
	AnimationState state;
	state.name = "Motion";
	state.clip = meta->guid;
	state.wrapMode = AnimationWrapMode::Once;
	player.groups = {{"First", {state}}, {"Second", {state}}};
	player.defaultGroup = "First";
	AnimationPlayerSystem system;

	// 自動再生と時刻指定を確認する
	system.Update(world, context);
	player.runtimePlayTime = 0.75f;
	player.runtimeSeekRequested = true;
	system.Update(world, context);
	if (player.runtimeCurrentGroup != "First" || world.GetComponent<TransformComponent>(entity).localPos.x != 7.5f) return false;

	// 停止後のSeekで時間進行を再開しない
	player.runtimeStopRequest = true;
	system.Update(world, context);
	player.runtimePlayTime = 0.25f;
	player.runtimeSeekRequested = true;
	system.Update(world, context);
	system.Update(world, context);
	if (player.runtimePlaying || world.GetComponent<TransformComponent>(entity).localPos.x != 2.5f) return false;

	// クロスフェード完了後は遷移元を残さない
	player.runtimePlayRequest = "Second";
	player.runtimePlayFade = 0.5f;
	system.Update(world, context);
	system.Update(world, context);
	if (player.runtimeCurrentGroup != "Second" || player.runtimeInTransition || !player.runtimeFromClips.empty()) return false;

	// グループを保ったまま単独Clipを再生する
	player.runtimeDirectClipRequest = meta->guid;
	player.runtimePlayTime = 0.5f;
	system.Update(world, context);
	if (player.groups.size() != 2 || world.GetComponent<TransformComponent>(entity).localPos.x != 5.0f) return false;
	player.runtimePlayRequest = "Missing";
	player.runtimePlayTime = 0.9f;
	context.deltaTime = 0.0f;
	system.Update(world, context);
	return world.GetComponent<TransformComponent>(entity).localPos.x == 5.0f;
}
