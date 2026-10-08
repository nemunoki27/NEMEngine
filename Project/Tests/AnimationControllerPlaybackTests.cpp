#include "AnimationControllerPlaybackTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Controllers/AnimationControllerManager.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Systems/Animation/AnimationPlayerSystem.h>
#include <Engine/Core/World/Systems/Animation/AnimationPlaybackRequests.h>
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Editor/Tools/Builtin/Animation/AnimationControllerEditSession.h>
#include <Engine/Editor/Tools/Builtin/Animation/AnimationControllerPreviewSession.h>

// c++
#include <fstream>

bool NEMTests::TestAnimationControllerPlayback() {

	using namespace Engine;
	RegisterBuiltinAnimationProperties();
	TestDirectory directory("AnimationController", RuntimePaths::GetGameAssetsRoot());
	const auto clipPath = directory.GetPath() / "Move.animclip.json";
	const auto controllerPath = directory.GetPath() / "Motion.animcontroller.json";
	AnimationClipAsset clip;
	clip.duration = 1.0f;
	clip.autoDuration = false;
	AnimationCurveTrack track;
	track.binding = { "Transform", "localPos", AnimationValueType::Vector3 };
	track.channels = MakeDefaultAnimationChannels(AnimationValueType::Vector3);
	track.channels[0].AddKey(0.0f, 0.0f);
	track.channels[0].AddKey(1.0f, 10.0f);
	clip.curveTracks.push_back(track);
	if (!SaveAnimationClipAsset(clipPath, clip)) return false;
	AnimationControllerAsset controller;
	controller.defaultState = "Idle";
	controller.states = { { "Idle", {} }, { "Move", {} } };
	controller.parameters = { { "Move", AnimationControllerParameterType::Trigger, false } };
	controller.transitions = { { "Idle", "Move", { { "Move", AnimationControllerConditionMode::If } }, 0.0f } };
	if (!SaveAnimationControllerAsset(controllerPath, controller)) return false;
	AssetDatabase database;
	database.Init();
	if (!database.RebuildMeta({ directory.GetPath() })) return false;
	const auto* clipMeta = database.FindByPath(RuntimePaths::ToAssetPath(clipPath));
	const auto* controllerMeta = database.FindByPath(RuntimePaths::ToAssetPath(controllerPath));
	if (!clipMeta || !controllerMeta) return false;
	const AssetID controllerID = controllerMeta->guid;
	// Editorで保存した定義を実行Systemへ渡す
	AnimationControllerEditSession session;
	if (!session.Select(database, controllerID)) return false;
	AnimationState state;
	state.name = "Motion";
	state.clip = clipMeta->guid;
	state.wrapMode = AnimationWrapMode::Once;
	session.GetDraft().states[1].states.push_back(state);
	session.MarkModified();
	if (!session.Save(database) || session.IsDirty()) return false;
	AnimationClipManager clips;
	AnimationControllerManager controllers;
	SystemContext context;
	context.assetDatabase = &database;
	context.animationClipManager = &clips;
	context.animationControllerManager = &controllers;
	context.mode = WorldMode::Play;
	context.deltaTime = 0.25f;
	ECSWorld world;
	const Entity entity = world.CreateEntity();
	world.AddComponent<TransformComponent>(entity);
	world.AddComponent<AnimationPlayerComponent>(entity);
	world.GetComponent<AnimationPlayerComponent>(entity).controller = controllerID;
	AnimationPlayerSystem system;
	system.Update(world, context);
	auto& player = world.GetComponent<AnimationPlayerComponent>(entity);
	if (player.runtimeCurrentGroup != "Idle") return false;
	const auto* definition = controllers.GetOrLoad(database, controllerID);
	if (!definition || !AnimationControllerEvaluator::SetParameter(definition->asset, player.runtimeController, "Move", true)) return false;
	system.Update(world, context);
	system.Update(world, context);
	if (player.runtimeCurrentGroup != "Move" || world.GetComponent<TransformComponent>(entity).localPos.x != 2.5f) return false;
	const auto generation = definition->generation;
	// Seekでは移動した範囲を再生せず、その時刻の姿勢だけを適用する
	player.runtimeNormalizedOffset = true;
	player.runtimePlayTime = 0.75f;
	player.runtimeSeekRequested = true;
	system.Update(world, context);
	if (world.GetComponent<TransformComponent>(entity).localPos.x != 7.5f || player.runtimeNormalizedTimeValue != 0.75f) return false;
	// Controllerを保持したまま単独Clipへ切り替える
	player.runtimeDirectClipRequest = state.clip;
	player.runtimePlayTime = 0.5f;
	system.Update(world, context);
	if (player.controller != controllerID || !player.runtimeControllerStopped ||
		world.GetComponent<TransformComponent>(entity).localPos.x != 5.0f) return false;
	// 不正な状態名で再生中のClipをSeekしない
	player.runtimePlayRequest = "Missing";
	player.runtimePlayTime = 0.9f;
	context.deltaTime = 0.0f;
	system.Update(world, context);
	if (world.GetComponent<TransformComponent>(entity).localPos.x != 5.0f) return false;
	// 再読込で増えたPropertyは適用前の値を保持する
	AnimationCurveTrack scaleTrack;
	scaleTrack.binding = { "Transform", "localScale", AnimationValueType::Vector3 };
	scaleTrack.channels = MakeDefaultAnimationChannels(AnimationValueType::Vector3);
	scaleTrack.channels[0].AddKey(0.0f, 2.0f);
	scaleTrack.channels[0].AddKey(1.0f, 4.0f);
	clip.curveTracks.push_back(scaleTrack);
	if (!SaveAnimationClipAsset(clipPath, clip)) return false;
	database.NotifyContentChanged(state.clip);
	player.runtimeSeekRequested = true;
	player.runtimePlayTime = 0.5f;
	system.Update(world, context);
	const AnimationPropertyBinding scaleBinding{ "Transform", "localScale", AnimationValueType::Vector3 };
	const auto capturedScale = std::find_if(player.runtimeBaseValues.begin(), player.runtimeBaseValues.end(), [&](const auto& base) {

		return base.binding.propertyPath == scaleBinding.propertyPath;
	});
	if (capturedScale == player.runtimeBaseValues.end() || std::get<Vector3>(capturedScale->value).x != 1.0f ||
		world.GetComponent<TransformComponent>(entity).localScale.x != 3.0f) return false;
	// Stop後のSeekは姿勢だけを変更し、時間進行を再開しない
	player.runtimeStopRequest = true;
	system.Update(world, context);
	player.runtimeSeekRequested = true;
	player.runtimePlayTime = 0.25f;
	system.Update(world, context);
	context.deltaTime = 0.25f;
	system.Update(world, context);
	if (player.runtimePlaying || world.GetComponent<TransformComponent>(entity).localPos.x != 2.5f) return false;
	world.GetComponent<TransformComponent>(entity).localPos.x = 5.0f;
	// 未保存定義のParameterと復元を確認する
	AnimationControllerPreviewSession preview;
	context.unscaledDeltaTime = 0.25f;
	if (!preview.Begin(world, entity, session.GetDraft())) return false;
	preview.Update(world, context);
	if (!preview.SetParameter("Move", true)) return false;
	preview.Update(world, context);
	preview.Update(world, context);
	if (preview.GetState() != "Move" || world.GetComponent<TransformComponent>(entity).localPos.x != 2.5f) return false;
	ECSWorld replacement;
	preview.Update(replacement, context);
	if (preview.IsActive() || world.GetComponent<TransformComponent>(entity).localPos.x != 5.0f) return false;
	// Worldの破棄後は復元先を参照しない
	auto temporary = std::make_unique<ECSWorld>();
	const Entity temporaryEntity = temporary->CreateEntity();
	temporary->AddComponent<TransformComponent>(temporaryEntity);
	if (!preview.Begin(*temporary, temporaryEntity, session.GetDraft())) return false;
	preview.Update(*temporary, context);
	temporary.reset();
	preview.End();
	// 壊れた再読込で公開済み定義と未保存編集を失わない
	std::ofstream(controllerPath) << "{";
	database.NotifyContentChanged(controllerID);
	definition = controllers.GetOrLoad(database, controllerID);
	session.MarkModified();
	return definition && definition->generation == generation &&
		!session.Select(database, controllerID) && session.IsDirty() && session.GetAssetID() == controllerID;
}
