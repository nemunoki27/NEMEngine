#include "TextCharAnimationTests.h"
#include "TestFixtures.h"

#include <Engine/Editor/Commands/Components/SetSerializedComponentCommand.h>

#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>
#include <Engine/Core/Assets/Database/AssetDependencyScanner.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>

#include <Engine/Core/Animation/Clips/AnimationClipAssetOverride.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Animation/Text/TextCharAnimationTiming.h>
#include <Engine/Core/World/Components/Animation/SimpleAnimationComponent.h>
#include <Engine/Core/World/Systems/Animation/TextCharAnimationSystem.h>
#include <algorithm>
#include <cmath>
#include <iostream>

bool NEMTests::TestTextCharAnimation() {

	using namespace Engine;
	auto almostEqual = [](float a, float b) { return std::abs(a - b) < 0.0001f; };
	TextRendererComponent renderer;
	TextLayoutRuntimeComponent glyphLayout;
	glyphLayout.boundsSize = {80, 40};
	renderer.pivot = {0.5f, 0.5f};
	TextLayoutGlyph glyph;
	glyph.rectMin = {50, 30};
	glyph.rectMax = {70, 50};
	TextCharTransform transform;
	transform.scale = {2, 3};
	TextCharAnimationPose animation;
	animation.transform.rotation = 90;
	auto pivotStaysFixed = [&](Vector3 expected) {
		const auto geometry = ResolveTextGlyphGeometry(renderer, glyphLayout, glyph, &transform, &animation, Matrix4x4::Identity());
		const auto point = Vector3::Transform(expected, geometry.worldMatrix);
		return almostEqual(point.x, expected.x) && almostEqual(point.y, expected.y);
	};
	if (!pivotStaysFixed({20, 20, 0})) return false;
	renderer.charPivot = {0, 1};
	if (!pivotStaysFixed({10, 30, 0})) return false;
	nlohmann::json savedRenderer = renderer;
	if (savedRenderer.get<TextRendererComponent>().charPivot != renderer.charPivot) return false;
	savedRenderer.erase("charPivot");
	from_json(savedRenderer, renderer);
	if (!pivotStaysFixed({20, 20, 0})) return false;

	TextCharAnimationComponent settings;
	TextCharAnimationSchedule schedule;
	std::vector<TextLayoutGlyph> glyphs(5);
	settings.interval = 0.1f;
	auto sample = [&](TextCharAnimationOrder order, std::initializer_list<float> times) {
		settings.order = order;
		UpdateTextCharSchedule(schedule, settings, glyphs, 1, 0.4f);
		return std::equal(schedule.starts.begin(), schedule.starts.end(), times.begin(), times.end(), almostEqual);
	};
	if (!sample(TextCharAnimationOrder::LeftToRight, {0, .1f, .2f, .3f, .4f}) || !almostEqual(schedule.duration,.8f) ||
		!sample(TextCharAnimationOrder::RightToLeft, {.4f,.3f,.2f,.1f,0}) ||
		!sample(TextCharAnimationOrder::EndsToCenter, {0,.1f,.2f,.1f,0}) ||
		!sample(TextCharAnimationOrder::CenterToEnds, {.2f,.1f,0,.1f,.2f}) || !almostEqual(schedule.duration,.6f) ||
		!sample(TextCharAnimationOrder::Simultaneous, {0,0,0,0,0})) return false;
	glyphs.resize(4);
	if (!sample(TextCharAnimationOrder::CenterToEnds, {.1f,0,0,.1f})) return false;
	glyphs[3].lineIndex = 1;
	settings.lines = TextCharAnimationLines::PerLine;
	if (!sample(TextCharAnimationOrder::LeftToRight, {0,.1f,.2f,0})) return false;
	settings.easing = EasingType::EaseOutQuad;
	if (!sample(TextCharAnimationOrder::LeftToRight, {0,.15f,.2f,0})) return false;
	settings.timing = TextCharAnimationTiming::TotalDuration;
	settings.totalDuration = .25f;
	if (!sample(TextCharAnimationOrder::LeftToRight, {0,0,0,0}) || !almostEqual(schedule.duration,.4f)) return false;
	settings.totalDuration = 1;
	if (!sample(TextCharAnimationOrder::LeftToRight, {0,.45f,.6f,0})) return false;
	settings.order = TextCharAnimationOrder::Random;
	settings.lines = TextCharAnimationLines::WholeText;
	settings.seed = 42;
	UpdateTextCharSchedule(schedule, settings, glyphs, 2, .4f);
	const auto ranks = schedule.starts;
	UpdateTextCharSchedule(schedule, settings, glyphs, 3, .4f);
	if (schedule.starts != ranks || nlohmann::json(settings).get<TextCharAnimationComponent>().seed != 42) return false;
	glyphs.resize(1);
	if (!sample(TextCharAnimationOrder::CenterToEnds, {0})) return false;
	glyphs.clear();
	if (!sample(TextCharAnimationOrder::Simultaneous, {}) || schedule.duration != 0) return false;

	ECSWorld world;
	const Entity source = world.CreateEntity(), text = world.CreateEntity();
	world.AddComponent<SimpleAnimationComponent>(source);
	auto& curve = world.GetComponent<SimpleAnimationComponent>(source).settings;
	curve = MakeSimpleAnimationSettings(0);
	curve.end = 1.0f;
	curve.duration = 0.4;
	world.AddComponent<TextRendererComponent>(text);
	world.AddComponent<TextCharAnimationComponent>(text);
	glyphs.resize(3);
	SetTextLayoutGlyphs(world, text, glyphs);
	auto& layout = world.GetComponent<TextLayoutRuntimeComponent>(text);
	layout.valid = true;
	++layout.layoutRevision;
	auto& c = world.GetComponent<TextCharAnimationComponent>(text);
	c.runtimeColorSourceID = world.GetUUID(source);
	c.interval = .1f;
	SystemContext context;
	context.mode = WorldMode::Play;
	context.deltaTime = context.unscaledDeltaTime = .1f;
	TextCharAnimationSystem system;
	auto tick = [&] { system.Update(world, context); };
	tick();
	if (!c.runtimeApplied || c.runtimePoses.size() != 3 || !almostEqual(c.runtimeDuration,.6f)) return false;
	tick();
	if (!almostEqual(c.runtimePoses[0].color.a,.25f) || !almostEqual(c.runtimePoses[1].color.a,0)) return false;
	c.runtimeRequest = 2;
	tick();
	if (!c.runtimePaused || !almostEqual(c.runtimeTime,.1f)) return false;
	c.runtimeRequest = 6;
	c.runtimeSeekTime = .32f;
	tick();
	if (!almostEqual(c.runtimePoses[2].color.a,.3f)) return false;
	c.runtimeRequest = 6;
	c.runtimeSeekTime = .04f;
	tick();
	if (!almostEqual(c.runtimePoses[0].color.a,.1f) || !almostEqual(c.runtimePoses[2].color.a,0)) return false;
	c.runtimeRequest = 3;
	for (int i = 0; i < 8; ++i) tick();
	if (!c.runtimeFinished || !almostEqual(c.runtimePoses[2].color.a,1)) return false;
	c.repeat = TextCharAnimationRepeat::LoopText;
	c.runtimeRequest = 1;
	tick();
	for (int i = 0; i < 7; ++i) tick();
	if (!almostEqual(c.runtimeTime,.1f)) return false;
	c.repeat = TextCharAnimationRepeat::LoopCharacters;
	c.runtimeRequest = 6;
	c.runtimeSeekTime = .65f;
	tick();
	if (!almostEqual(c.runtimePoses[2].color.a,.125f)) return false;
	const double clock = c.runtimeClock;
	glyphs.resize(5);
	SetTextLayoutGlyphs(world, text, glyphs);
	++layout.layoutRevision;
	c.runtimeRequest = 2;
	tick();
	if (c.runtimePoses.size() != 5 || c.runtimeClock != clock || c.runtimeLayoutRevision != layout.layoutRevision) return false;
	c.runtimeRequest = 4;
	tick();
	if (c.runtimeApplied || !c.runtimePoses.empty()) return false;
	context.mode = WorldMode::Edit;
	tick();
	if (c.runtimeApplied) return false;
	c.playInEditMode = true;
	tick();
	if (!c.runtimeApplied) return false;
	c.enabled = false;
	tick();
	return !c.runtimeApplied && c.runtimePoses.empty();
}

bool NEMTests::TestTextCharAnimationManaged() {

	using namespace Engine;
	TextCharAnimationComponent original;
	const Engine::UUID originalID = Engine::UUID::New();
	const Engine::UUID prefabID = Engine::UUID::New();
	const auto clipID = AssetID::New();
	original.translationSource.localFileID = originalID;
	original.rotationSource = original.scaleSource = original.colorSource = original.translationSource;
	nlohmann::json saved = original;
	AssetDependencyScanner::IDReferences ids;
	AssetDependencyScanner::PathReferences paths;
	AssetDependencyScanner::ScanReferences({{"TextCharAnimation", nlohmann::json(TextCharAnimationComponent{})}}, ids, paths);
	if (!ids.empty() || !paths.empty()) return false;
	AssetDependencyScanner::ScanReferences({{"TextCharAnimation", {{"clip", ToString(clipID)}}}}, ids, paths);
	if (ids.find(clipID) == ids.end() || ids.find(clipID)->second != AssetType::AnimationClip) return false;
	TextCharAnimationComponent mixed;
	mixed.translationSource = original.translationSource;
	mixed.sourceAssets[2] = clipID;
	mixed.sourceAssets[3] = AssetID::New();
	const nlohmann::json serialized = mixed;
	auto roundTrip = serialized.get<TextCharAnimationComponent>();
	if (roundTrip.sourceAssets != mixed.sourceAssets || roundTrip.translationSource.localFileID != originalID ||
		serialized.contains("source") || serialized.contains("clip")) return false;
	ids.clear();
	AssetDependencyScanner::ScanReferences({{"TextCharAnimation", serialized}}, ids, paths);
	if (!ids.contains(mixed.sourceAssets[2]) || !ids.contains(mixed.sourceAssets[3])) return false;

	auto legacy = saved;
	legacy["source"] = "AnimationClip";
	legacy["clip"] = ToString(clipID);
	auto pending = legacy.get<TextCharAnimationComponent>();
	if (pending.translationSource.localFileID || pending.legacyClip != clipID ||
		nlohmann::json(pending).at("clip") != ToString(clipID)) return false;
	ECSWorld migrationWorld;
	AssetDatabase database;
	AnimationClipManager clips;
	auto asset = std::make_shared<AnimationClipAsset>();
	AnimationCurveTrack scale;
	scale.binding = {"Transform", "localScale2D", AnimationValueType::Vector2};
	scale.channels = MakeDefaultAnimationChannels(scale.binding.valueType);
	scale.channels.front().AddKey(0, 1);
	asset->curveTracks.emplace_back(std::move(scale));
	if (!PublishAnimationClipAssetOverride(migrationWorld, clipID, asset) ||
		!ResolveLegacyTextCharClip(migrationWorld, database, clips, pending) || pending.legacyClip ||
		pending.sourceAssets[0] || pending.sourceAssets[1] || pending.sourceAssets[2] != clipID || pending.sourceAssets[3]) return false;
	const auto owner = migrationWorld.CreateEntity();
	migrationWorld.AddComponent<TextCharAnimationComponent>(owner);
	EditorContext editor;
	editor.activeWorld = &migrationWorld;
	EditorCommandContext context;
	context.editorContext = &editor;
	SetSerializedComponentCommand change(owner, "TextCharAnimation", saved, serialized);
	if (!change.Execute(context) || migrationWorld.GetComponent<TextCharAnimationComponent>(owner).sourceAssets != mixed.sourceAssets) return false;
	change.Undo(context);
	if (nlohmann::json(migrationWorld.GetComponent<TextCharAnimationComponent>(owner)) != saved ||
		!change.Redo(context) || nlohmann::json(migrationWorld.GetComponent<TextCharAnimationComponent>(owner)) != serialized) return false;

	PrefabReferenceRemapper::RemapComponent("TextCharAnimation", saved, {{originalID, prefabID}},
		PrefabReferenceRemapper::ReferenceSpace::Prefab, {});
	auto restored = saved.get<TextCharAnimationComponent>();
	for (const auto* ref : {&restored.translationSource, &restored.rotationSource, &restored.scaleSource, &restored.colorSource}) {
		if (ref->localFileID != prefabID || !ref->prefab) return false;
	}

	return RunManagedComponentProbe("TextCharAnimation", "NEMTests.Scripts.TextCharAnimationAPIProbe",
		"39beccc7-e4c3-448d-bf96-b345a3ac0d45", "dc999f78-45e2-41e5-b518-bc370a493cda", [](Engine::ECSWorld& world) {
			const auto source = Engine::SceneAuthoring::CreateGameObject(world, "AnimationSource");
			world.AddComponent<Engine::SimpleAnimationComponent>(source);
			world.GetComponent<SimpleAnimationComponent>(source).settings = MakeSimpleAnimationSettings(1);
			const auto scalar = SceneAuthoring::CreateGameObject(world, "ScalarSource");
			world.AddComponent<SimpleAnimationComponent>(scalar);

			AssetDatabase assets;
			PrefabSystem prefabs;
			const std::string prefabPath = "GameAssets/Source.prefab.json";
			if (!assets.Init() || !prefabs.SavePrefab(assets, world, source, prefabPath))
				throw std::runtime_error("参照Prefabを作成できません");
			const auto* prefab = assets.FindByPath(prefabPath);
			if (!prefab) throw std::runtime_error("参照Prefabがありません");
			const auto prefabID = prefab->guid;
			AnimationClipAsset clip;
			clip.name = "Color";
			auto& track = clip.curveTracks.emplace_back();
			track.binding = {"TextRenderer", "color", AnimationValueType::Color4};
			track.channels = MakeDefaultAnimationChannels(AnimationValueType::Color4);
			track.channels[3].AddKey(0, 1);
			const std::string clipPath = "GameAssets/Color.animClip";
			if (!SaveAnimationClipAsset(clipPath, clip)) throw std::runtime_error("参照クリップを保存できません");
			const auto clipID = assets.ImportOrGet(clipPath, AssetType::AnimationClip);
			if (!clipID) throw std::runtime_error("参照クリップを登録できません");
			world.AddComponent<TextCharAnimationComponent>(source);
			auto& refs = world.GetComponent<TextCharAnimationComponent>(source).sourceAssets;
			refs[2] = prefabID;
			refs[3] = clipID;
		}, [](Engine::AssetDatabase& assets) {
			return assets.ImportOrGet("GameAssets/Source.prefab.json", AssetType::Prefab) &&
				assets.ImportOrGet("GameAssets/Color.animClip", AssetType::AnimationClip);
		});
}
