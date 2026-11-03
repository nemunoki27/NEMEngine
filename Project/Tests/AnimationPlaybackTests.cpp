#include "AnimationPlaybackTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Systems/Animation/AnimationClipPlayerSystem.h>
#include <Engine/Core/World/Systems/Animation/MotionAnimationSystem.h>
#include <Engine/Core/World/Components/Animation/AnimationClipPlayerComponent.h>
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
	world.AddComponent<AnimationClipPlayerComponent>(entity);
	auto& player = world.GetComponent<AnimationClipPlayerComponent>(entity);
	player.clip = meta->guid;
	player.wrapMode = AnimationWrapMode::Once;
	AnimationClipPlayerSystem system;
	system.Update(world, context);
	player.runtimeSeekTime = 0.75f;
	player.runtimeRequest = 6;
	system.Update(world, context);
	if (!player.runtimeApplied || world.GetComponent<TransformComponent>(entity).localPos.x != 7.5f) return false;
	player.runtimeRequest = 2;
	system.Update(world, context);
	if (player.runtimeApplied || !player.runtimePaused || player.runtimePlaying) return false;
	player.runtimeRequest = 3;
	system.Update(world, context);
	if (!player.runtimeApplied || !player.runtimeFinished || world.GetComponent<TransformComponent>(entity).localPos.x != 10.0f) return false;
	player.runtimeRequest = 4;
	system.Update(world, context);
	if (player.runtimeApplied) return false;
	player.runtimeSeekTime = 0.25f;
	player.runtimeRequest = 6;
	system.Update(world, context);
	if (!player.runtimeApplied) return false;
	system.Update(world, context);
	if (player.runtimeApplied || player.runtimePlaying || world.GetComponent<TransformComponent>(entity).localPos.x != 2.5f) return false;
	player.runtimeRequest = 5;
	system.Update(world, context);
	if (!player.runtimeApplied || world.GetComponent<TransformComponent>(entity).localPos.x != 0.0f) return false;
	if (nlohmann::json(player).contains("runtimeApplied")) return false;
	player.runtimeRequest = 1;
	system.Update(world, context);
	if (!player.runtimePlaying || world.GetComponent<TransformComponent>(entity).localPos.x != 2.5f) return false;
	world.AddComponent<MotionAnimationComponent>(entity);
	auto* motion = TryGetMotionAnimation(world, entity);
	motion->settings.offset = {100, 0, 0};
	motion->settings.duration = 10;
	MotionAnimationSystem motionSystem;
	motionSystem.Update(world, context);
	auto& controlled = world.GetComponent<AnimationClipPlayerComponent>(entity);
	controlled.runtimeRequest = 4;
	system.Update(world, context);
	controlled.runtimeSeekTime = 0.75f;
	controlled.runtimeRequest = 6;
	system.Update(world, context);
	motionSystem.Update(world, context);
	if (!controlled.runtimeApplied || motion->runtime.diagnostic.empty() ||
		world.GetComponent<TransformComponent>(entity).localPos.x != 7.5f) return false;
	controlled.runtimeRequest = 5;
	system.Update(world, context);
	motionSystem.Update(world, context);
	if (!controlled.runtimeApplied || world.GetComponent<TransformComponent>(entity).localPos.x != 0.0f) return false;
	controlled.runtimeRequest = 1;
	for (int i = 0; i < 4; ++i) system.Update(world, context);
	motionSystem.Update(world, context);
	if (!controlled.runtimeFinished || !controlled.runtimeApplied ||
		world.GetComponent<TransformComponent>(entity).localPos.x != 10.0f) return false;
	return true;
}
