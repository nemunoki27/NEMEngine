#include "TextCharAnimationSamplerTests.h"
#include "TestFixtures.h"

#include <Engine/Core/Animation/Text/TextCharAnimationSampler.h>
#include <Engine/Core/Animation/Clips/AnimationClipAssetOverride.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Animation/SimpleAnimationComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

// c++
#include <cmath>

bool NEMTests::TestTextCharAnimationSampler() {

	using namespace Engine;
	auto closeEnough = [](float a, float b) { return std::abs(a - b) < 0.0001f; };
	ECSWorld world;
	Entity owner = world.CreateEntity();
	Entity movement = world.CreateEntity();
	Entity fade = world.CreateEntity();
	world.AddComponent<SceneObjectComponent>(movement);
	world.GetComponent<SceneObjectComponent>(movement).localFileID = Engine::UUID{13};
	world.AddComponent<SimpleAnimationComponent>(movement);
	auto& motion = world.GetComponent<SimpleAnimationComponent>(movement);
	motion.settings = MakeSimpleAnimationSettings(1);
	motion.settings.start = Vector2(0, 8);
	motion.settings.end = Vector2(0, 0);
	motion.settings.duration = 1.0;
	motion.settings.speed = 2.0;
	motion.settings.repeat = SimpleAnimationRepeat::Loop;
	motion.runtime.time = 123.0;
	world.AddComponent<SimpleAnimationComponent>(fade);
	auto& opacity = world.GetComponent<SimpleAnimationComponent>(fade);
	opacity.settings = MakeSimpleAnimationSettings(0);
	opacity.settings.end = 1.0f;
	opacity.settings.duration = 0.25;

	TextCharAnimationComponent component;
	component.translationSource = SceneEntityReference::FromEntity(world, movement);
	component.runtimeColorSourceID = world.GetUUID(fade);
	component.runtimeRotationSourceID = world.GetUUID(movement);
	TextCharAnimationSampler sampler;
	SystemContext context;
	std::string diagnostic;
	if (!sampler.Prepare(world, owner, context, component, diagnostic) || !closeEnough(sampler.Duration(), 0.5f) ||
		diagnostic.find("Z回転") == std::string::npos) return false;
	auto pose = sampler.Sample(0.25f, false);
	if (!closeEnough(pose.transform.translation.y, 4) || !closeEnough(pose.color.a, 1) || !closeEnough(pose.color.r, 1)) return false;
	pose = sampler.Sample(0.375f, true);
	if (!closeEnough(pose.transform.translation.y, 2) || !closeEnough(pose.color.a, 0.5f)) return false;
	pose = sampler.Sample(-1.0f, false);
	if (!closeEnough(pose.transform.translation.y, 8) || !closeEnough(pose.color.a, 0) ||
		world.GetComponent<SimpleAnimationComponent>(movement).runtime.time != 123.0) return false;

	auto& settings = world.GetComponent<SimpleAnimationComponent>(fade).settings;
	settings.mode = SimpleAnimationMode::Sin;
	settings.amplitude = 0.5f;
	settings.center = 0.5f;
	settings.period = 1.0;
	if (!sampler.Prepare(world, owner, context, component, diagnostic) ||
		!closeEnough(sampler.Sample(1.75f, true).color.a, 0) || !closeEnough(sampler.Sample(1.75f, false).color.a, 0.5f)) return false;

	AnimationClipManager manager;
	AssetDatabase database;
	context.assetDatabase = &database;
	context.animationClipManager = &manager;
	const AssetGUID clipID{0, 72};
	component.sourceAssets[1] = component.sourceAssets[2] = clipID;
	auto clip = std::make_shared<AnimationClipAsset>();
	clip->duration = 1;
	clip->loop = true;
	clip->loopBridge = {true, 0.5f, CurveInterpolationMode::Linear};
	auto makeTrack = [](const char* componentName, const char* path, AnimationValueType type, size_t channel, float start, float end) {
		AnimationCurveTrack track;
		track.binding = {componentName, path, type};
		track.channels = MakeDefaultAnimationChannels(type);
		track.channels[channel].AddKey(0, start);
		track.channels[channel].AddKey(1, end);
		return track;
	};
	clip->curveTracks.push_back(makeTrack("Transform", "localScale", AnimationValueType::Vector3, 1, 1, 3));
	clip->curveTracks.push_back(makeTrack("TextRenderer", "material.color", AnimationValueType::Color4, 3, 0, 1));
	clip->curveTracks.push_back(makeTrack("Transform", "localRotationZ", AnimationValueType::Float, 0, 0, 70));
	clip->curveTracks.push_back(makeTrack("SpriteRenderer", "color", AnimationValueType::Color4, 0, 1, 0));
	if (TextCharAnimationSampler::ClipChannelMask(*clip) != 0x0e ||
		!PublishAnimationClipAssetOverride(world, clipID, clip) ||
		!sampler.Prepare(world, owner, context, component, diagnostic) || !closeEnough(sampler.Duration(), 1.0f) ||
		static_cast<uint32_t>(sampler.Channels()) != (static_cast<uint32_t>(TimeChannel::SimpleAnimation) | static_cast<uint32_t>(TimeChannel::AnimationClip)) ||
		!diagnostic.empty()) return false;
	pose = sampler.Sample(10.0f, false);
	if (!closeEnough(pose.transform.scale.x, 1) || !closeEnough(pose.transform.scale.y, 3) || !closeEnough(pose.color.a, 0.5f) ||
		!closeEnough(pose.transform.rotation, 70) || !closeEnough(pose.color.r, 1)) return false;
	component.repeat = TextCharAnimationRepeat::LoopCharacters;
	if (!sampler.Prepare(world, owner, context, component, diagnostic) || !closeEnough(sampler.Duration(), 1.5f)) return false;
	pose = sampler.Sample(1.25f, true);
	if (!closeEnough(pose.color.a, 1) || !closeEnough(pose.transform.scale.y, 2) || !closeEnough(pose.transform.rotation, 35)) return false;
	if (!TextCharAnimationSampler::ValidateAsset(0, world, database, manager, clipID) ||
		TextCharAnimationSampler::ValidateAsset(1, world, database, manager, clipID)) return false;

	// 移動はシーン、回転とスケールはクリップ、色はPrefabのルートを参照する
	TestDirectory directory("TextCharAnimationPrefab", RuntimePaths::GetGameAssetsRoot());
	const auto path = directory.GetPath() / "Fade.prefab.json";
	SimpleAnimationComponent fadeTemplate;
	fadeTemplate.settings = MakeSimpleAnimationSettings(0);
	fadeTemplate.settings.duration = 0.5;
	const auto rootID = std::string("0000000000000001");
	nlohmann::json document{{"SchemaVersion", 2}, {"Header", {{"rootLocalFileID", rootID}}},
		{"Entities", nlohmann::json::array({
			{{"LocalFileID", "0000000000000002"}, {"Components", {{"SimpleAnimation", world.GetComponent<SimpleAnimationComponent>(movement)}}}},
			{{"LocalFileID", rootID}, {"Components", {{"SimpleAnimation", fadeTemplate}}}}
		})}};
	if (!JsonFile::Save(path, document) || !database.Init()) return false;
	const auto prefab = database.ImportOrGet(RuntimePaths::ToAssetPath(path), AssetType::Prefab);
	component.sourceAssets[3] = prefab;
	component.runtimeColorSourceID = {};
	if (!prefab || !TextCharAnimationSampler::ValidateAsset(0, world, database, manager, prefab) ||
		TextCharAnimationSampler::ValidateAsset(3, world, database, manager, prefab) ||
		!sampler.Prepare(world, owner, context, component, diagnostic) || !diagnostic.empty()) return false;
	pose = sampler.Sample(0.25f, false);
	if (!closeEnough(pose.color.a, .5f) || !closeEnough(pose.transform.translation.y, 4) ||
		!closeEnough(pose.transform.rotation, 17.5f) || !closeEnough(pose.transform.scale.y, 1.5f)) return false;

	document["Entities"][1]["Components"].erase("SimpleAnimation");
	if (!JsonFile::Save(path, document)) return false;
	database.NotifyContentChanged(prefab);
	if (!TextCharAnimationSampler::ValidateAsset(3, world, database, manager, prefab) ||
		!sampler.Prepare(world, owner, context, component, diagnostic) || diagnostic.empty() ||
		!closeEnough(sampler.Sample(.25f, false).color.a, 1)) return false;

	ClearAnimationClipAssetOverride(world, clipID);
	return sampler.Prepare(world, owner, context, component, diagnostic) && sampler.Channels() == TimeChannel::SimpleAnimation &&
		closeEnough(sampler.Sample(.25f, false).transform.translation.y, 4);
}
