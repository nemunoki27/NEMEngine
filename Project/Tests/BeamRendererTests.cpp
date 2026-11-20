#include "BeamRendererTests.h"
#include "TestFixtures.h"
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>

#include <Engine/Core/World/Components/Rendering/BeamRendererComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Systems/Rendering/BeamRendererSystem.h>

bool NEMTests::TestBeamRenderer() {

	using namespace Engine;
	ECSWorld world;
	const Entity owner = world.CreateEntity();
	world.AddComponent<TransformComponent>(owner);
	world.AddComponent<BeamRendererComponent>(owner);
	auto* record = TryGetBeamRenderer(world, owner);
	if (!record || record->settings.material != BuiltinAssets::Materials::DefaultBeam) return false;
	auto settings = record->settings;
	if (settings.useWidthCurve) return false;
	auto savedSettings = nlohmann::json(settings);
	if (savedSettings.contains("uvScrollSpeed") || savedSettings.contains("textureMode") ||
		savedSettings.get<BeamRendererSettings>().useWidthCurve) return false;
	if (savedSettings.contains("colorGradient")) return false;
	auto legacy = savedSettings;
	legacy["material"] = ToAssetReferenceJson(BuiltinAssets::Materials::DefaultTrail);
	CurveColor4 gradient;
	gradient.channels[0].AddKey(0, 1);
	gradient.channels[0].AddKey(1, 0);
	legacy["colorGradient"] = gradient.channels;
	const auto migrated = legacy.get<BeamRendererSettings>();
	if (migrated.material != BuiltinAssets::Materials::DefaultBeam) return false;
	for (const auto& [name, t] : {std::pair{"startColor", 0.0f}, std::pair{"targetColor", 1.0f}}) {
		const auto* value = migrated.materialInstance.Find(MaterialParameterID::FromName(name));
		if (!value || std::get<Color4>(value->value) != gradient.Evaluate(t)) return false;
	}
	auto edited = migrated;
	edited.materialInstance.Set("targetColor", MaterialParameterValue{.value = Color4::White()});
	const auto roundTrip = nlohmann::json(edited).get<BeamRendererSettings>();
	const auto targetID = MaterialParameterID::FromName("targetColor");
	if (std::get<Color4>(roundTrip.materialInstance.Find(targetID)->value) != Color4::White() ||
		std::get<Color4>(migrated.materialInstance.Find(targetID)->value) != gradient.Evaluate(1.0f)) return false;
	legacy["material"] = "";
	if (legacy.get<BeamRendererSettings>().material != BuiltinAssets::Materials::DefaultBeam) return false;
	savedSettings.erase("useWidthCurve");
	if (!savedSettings.get<BeamRendererSettings>().useWidthCurve) return false;
	settings.is2D = true;
	SetBeamRendererSettings(world, owner, settings);
	PlayBeam(world, owner);
	auto& runtime = record->runtime;
	if (runtime.points.size() != 17 || runtime.points.back().position != Vector3(3, 0, 0)) return false;
	settings = record->settings;
	settings.mode = BeamMode::Lightning;
	settings.waypoints = {{BeamEndpointMode::World, {0, 2, 9}}};
	SetBeamRendererSettings(world, owner, settings);
	UpdateBeamGeometry(world, owner, *record);
	const auto points = runtime.points;
	if (points.size() != 48 || points[16].position != Vector3(0, 2, 0) || points[32].progress != 1) return false;
	runtime.time = 1;
	UpdateBeamGeometry(world, owner, *record);
	if (runtime.points[1].position == points[1].position) return false;
	runtime.time = 0;
	UpdateBeamGeometry(world, owner, *record);
	for (size_t i = 0; i < points.size(); ++i) {
		if (runtime.points[i].position != points[i].position || points[i].position.z != 0) return false;
	}
	settings.end.mode = BeamEndpointMode::Object;
	settings.branchCount = 0;
	SetBeamRendererSettings(world, owner, settings);
	UpdateBeamGeometry(world, owner, *record);
	if (runtime.points.size() != 17 || runtime.points.back().position != Vector3(0, 2, 0)) return false;
	auto saved = nlohmann::json(record->settings).get<BeamRendererSettings>();
	if (saved.waypoints.size() != 1 || saved.mode != BeamMode::Lightning || !saved.is2D) return false;
	const Entity wall = world.CreateEntity();
	auto& wallTransform = world.AddComponent<TransformComponent>(wall);
	wallTransform.localPos = {2, 2, 0};
	wallTransform.worldMatrix = Matrix4x4::MakeTranslateMatrix(wallTransform.localPos);
	auto& collider = world.AddComponent<CollisionComponent>(wall);
	collider.shape.type = ColliderShapeType::Sphere3D;
	collider.shape.radius = 0.5f;
	settings.is2D = false;
	settings.mode = BeamMode::Beam;
	settings.end = {BeamEndpointMode::World, {4, 2, 0}};
	settings.waypoints[0].position.z = 0;
	settings.stopOnHit = true;
	SetBeamRendererSettings(world, owner, settings);
	UpdateBeamGeometry(world, owner, *record);
	if (!runtime.hasHit || runtime.hit.entity != wall || runtime.hitSegment != 1 ||
		(runtime.points.back().position - Vector3(1.5f, 2, 0)).Length() > 0.0001f) return false;
	settings.end = {BeamEndpointMode::Object, {}, {}, wall};
	SetBeamRendererSettings(world, owner, settings);
	UpdateBeamGeometry(world, owner, *record);
	if (runtime.hasHit || runtime.points.back().position != Vector3(2, 2, 0)) return false;
	settings.playbackMode = BeamPlaybackMode::OneShot;
	settings.duration = 0.1f;
	SetBeamRendererSettings(world, owner, settings);
	BeamRendererSystem system;
	SystemContext context;
	context.mode = WorldMode::Play;
	context.deltaTime = context.unscaledDeltaTime = 0.2f;
	system.LateUpdate(world, context);
	return !runtime.playing && runtime.points.empty() && !runtime.hasHit;
}

bool NEMTests::TestBeamRendererManaged() {

	return RunManagedComponentProbe("BeamRenderer", "NEMTests.Scripts.BeamAPIProbe",
		"06480fab-cb1d-4a63-9951-749fcd294058", "475d100d-755d-45b0-b4b0-b05421caf70f",
		[](Engine::ECSWorld& world) {
			const auto target = Engine::SceneAuthoring::CreateGameObject(world, "RuntimeTarget");
			world.GetComponent<Engine::SceneObjectComponent>(target).localFileID = {};
			world.AddComponent<Engine::BeamRendererComponent>(target);
			auto& instance = Engine::TryGetBeamRenderer(world, target)->settings.materialInstance;
			instance.Set("baseColorTexture", Engine::MaterialParameterValue{.value = Engine::BuiltinAssets::Textures::RectLTCMatrix});
			const auto wall = Engine::SceneAuthoring::CreateGameObject(world, "BeamWall");
			world.AddComponent<Engine::CollisionComponent>(wall);
			world.GetComponent<Engine::CollisionComponent>(wall).shape.offset = {4, 0, 0};
		}, [](Engine::AssetDatabase& database) {
			return database.ImportOrGet("engine://Rendering/Builtin/Particle/Beam/defaultBeam.material.json", Engine::AssetType::Material) == Engine::BuiltinAssets::Materials::DefaultBeam &&
				database.ImportOrGet("engine://Textures/Lighting/LTC/ltcMatrix.dds", Engine::AssetType::Texture) == Engine::BuiltinAssets::Textures::RectLTCMatrix;
		});
}
