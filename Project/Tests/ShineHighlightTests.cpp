#include "ShineHighlightTests.h"
#include "TestFixtures.h"

#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>
#include <Engine/Core/Animation/Simple/SimpleAnimationPlayback.h>
#include <Engine/Core/World/Components/Rendering/ShineHighlightComponent.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Animation/SimpleAnimationSystem.h>
#include <Engine/Editor/Commands/Components/SetSerializedComponentCommand.h>
#include <Engine/Editor/Commands/Core/EditorCommandContext.h>

bool NEMTests::TestShineHighlight() {

	using namespace Engine;
	RegisterBuiltinAnimationProperties();
	ECSWorld world;
	const auto object = SceneAuthoring::CreateGameObject(world, "Shine");
	world.AddComponent<ShineHighlightComponent>(object);
	auto& shine = world.GetComponent<ShineHighlightComponent>(object);
	shine.position = 0.875f;
	shine.angle = -30.0f;
	shine.width = 0.25f;
	shine.rampTexture = AssetID::New();
	const auto ramp = shine.rampTexture;
	nlohmann::json saved;
	if (!world.SerializeComponentToJson(object, "ShineHighlight", saved)) return false;
	const auto copy = world.CreateEntity();
	if (!world.AddComponentFromJson(copy, "ShineHighlight", saved)) return false;
	const auto& restored = world.GetComponent<ShineHighlightComponent>(copy);
	if (restored.position != 0.875f || restored.angle != -30.0f || restored.width != 0.25f || restored.rampTexture != ramp) return false;

	EditorContext editor;
	editor.activeWorld = &world;
	EditorCommandContext commandContext;
	commandContext.editorContext = &editor;
	auto after = saved;
	after["width"] = 0.125f;
	SetSerializedComponentCommand edit(object, "ShineHighlight", saved, after);
	if (!edit.Execute(commandContext) || world.GetComponent<ShineHighlightComponent>(object).width != 0.125f) return false;
	edit.Undo(commandContext);
	if (world.GetComponent<ShineHighlightComponent>(object).width != 0.25f || !edit.Redo(commandContext) ||
		world.GetComponent<ShineHighlightComponent>(object).width != 0.125f) return false;

	AnimationCurveTrack track;
	track.binding = {"ShineHighlight", "position", AnimationValueType::Float};
	track.channels = MakeDefaultAnimationChannels(AnimationValueType::Float);
	track.channels[0].AddKey(0.0f, 0.0f);
	track.channels[0].AddKey(1.0f, 1.0f);
	AnimationClipAsset clip;
	clip.duration = 1.0f;
	clip.curveTracks.push_back(track);
	AnimationClipEvaluator::ApplyClip(world, object, clip, 0.5f, {});
	if (world.GetComponent<ShineHighlightComponent>(object).position != 0.5f) return false;

	world.AddComponent<SimpleAnimationComponent>(object);
	auto& animation = world.GetComponent<SimpleAnimationComponent>(object);
	animation.settings = MakeSimpleAnimationSettings(0);
	animation.settings.end = 1.0f;
	animation.autoApply = true;
	animation.binding = track.binding;
	SystemContext context;
	context.mode = WorldMode::Play;
	context.deltaTime = context.unscaledDeltaTime = 0.25f;
	SimpleAnimationSystem system;
	system.Update(world, context);
	system.Update(world, context);
	return world.GetComponent<ShineHighlightComponent>(object).position == 0.25f &&
		world.GetComponent<ShineHighlightComponent>(copy).position == 0.875f;
}

bool NEMTests::TestShineHighlightManaged() {

	return RunManagedComponentProbe("ShineHighlight", "NEMTests.Scripts.ShineHighlightAPIProbe",
		"d6705f15-b1bf-4781-8959-c91772804054", "70895dae-b297-4cd5-aba6-9454b9a86185",
		[](Engine::ECSWorld& world) {
			const auto source = Engine::SceneAuthoring::CreateGameObject(world, "RampSource");
			world.AddComponent<Engine::ShineHighlightComponent>(source);
			world.GetComponent<Engine::ShineHighlightComponent>(source).rampTexture = Engine::BuiltinAssets::Textures::RectLTCMatrix;
		}, [](Engine::AssetDatabase& database) {
			using namespace Engine;
			const auto ramp = database.ImportOrGet("engine://Textures/Lighting/LTC/ltcMatrix.dds", AssetType::Texture);
			if (ramp != BuiltinAssets::Textures::RectLTCMatrix) return false;
			ECSWorld world;
			const auto owner = SceneAuthoring::CreateGameObject(world, "Shine");
			world.AddComponent<ShineHighlightComponent>(owner);
			auto& shine = world.GetComponent<ShineHighlightComponent>(owner);
			shine.position = 0.75f;
			shine.rampTexture = ramp;
			const std::string path = "GameAssets/Shine.prefab.json";
			PrefabSystem prefabs;
			HierarchySystem hierarchy;
			PrefabInstantiateResult spawned;
			if (!prefabs.SavePrefab(database, world, owner, path)) return false;
			const auto* record = database.FindByPath(path);
			if (!record || !prefabs.InstantiatePrefab(database, hierarchy, world, record->guid, spawned) ||
				spawned.createdEntities.size() != 1) return false;
			const auto* restored = world.TryGetComponent<ShineHighlightComponent>(spawned.createdEntities.front());
			return restored && restored->rampTexture == ramp && restored->position == 0.75f;
		});
}
