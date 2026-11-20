#include "DecalTests.h"
#include "TestFixtures.h"
#include "TestRunner.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Decals/DecalCollector.h>
#include <Engine/Core/Rendering/Decals/DecalGraphProgram.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Rendering/Decals/DecalSettingsMigration.h>
#include <Engine/Core/World/Components/Rendering/UVTransformComponent.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Decals/DecalProjection.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshBoundsUtility.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Editor/Assets/Project/ProjectAssetOperations.h>

// c++
#include <cmath>
#include <limits>

namespace {

	using namespace Engine;

	bool Near(float a, float b) { return std::abs(a - b) < 1e-4f; }

	bool CheckGraphMaterial() {

		auto graph = CreateDefaultSurfaceShaderGraph("Decal", ShaderGraphTarget::Decal);
		const auto graphID = AssetID::New();
		ShaderGraphArtifact artifact;
		if (!ShaderGraphArtifactCache::Compile(graph, graphID, artifact) || !artifact.compileOutput.decalGraph ||
			artifact.opaquePipelineID || artifact.rayTracingPipelineID) return false;
		auto material = ShaderGraphArtifactCache::CreateMaterial(graph, graphID);
		ShaderGraphArtifactCache::ApplyToMaterial(artifact, material);
		MaterialAsset restored;
		if (!FromJson(ToJson(material), restored) || restored.domain != MaterialDomain::Decal ||
			!restored.decalGraph || restored.decalGraph->channels != 1 ||
			restored.decalGraph->instructions.size() != 2) return false;
		auto encoded = WriteDecalGraph(*restored.decalGraph);
		if (encoded != WriteDecalGraph(*artifact.compileOutput.decalGraph)) return false;
		encoded["instructions"][1]["inputs"] = UINT32_MAX;
		DecalGraphProgram invalid;
		return !ReadDecalGraph(encoded, invalid) && !ReadDecalGraph(nlohmann::json{{"version", 1}}, invalid) &&
			!FromJson(nlohmann::json{{"domain", "Decal"}, {"decalGraph", 1}}, restored);
	}

	// 親の負ScaleとNear面を跨いでも投影を欠かさない
	bool CheckProjection() {

		DecalProjectorComponent component;
		component.size = Vector3(2, 4, 0.5f);
		component.pivot = Vector3(1, 0, 0);
		DecalProjection projection;
		const auto parent = Matrix4x4::MakeAffineMatrix(Vector3(-2, 3, 1), Vector3(0, 35, 0), Vector3(4, 1, 8));
		if (!DecalProjectionUtility::Build(component, parent, projection)) return false;
		const auto point = Vector3::Transform(Vector3(0.25f, -0.25f, 0.4f), projection.decalToWorld);
		const auto local = Vector3::Transform(point, projection.worldToDecal);
		if (!Near(local.x, 0.25f) || !Near(local.y, -0.25f) || !Near(local.z, 0.4f) ||
			!DecalProjectionUtility::Intersects(projection.worldToDecal, point, 0.0f) ||
			DecalProjectionUtility::Intersects(projection.worldToDecal, Vector3(100, 100, 100), 1.0f)) return false;
		component.pivot = {};
		component.size = Vector3(1, 1, 1);
		const auto camera = Matrix4x4::MakePerspectiveFovMatrix(60, 1, 0.1f, 100);
		int32_t left, top, right, bottom;
		if (!DecalProjectionUtility::Build(component, Matrix4x4::MakeTranslateMatrix(Vector3(0, 0, -2)), projection) ||
			DecalProjectionUtility::GetScreenRect(projection, camera, 100, 100, left, top, right, bottom)) return false;
		if (!DecalProjectionUtility::Build(component, Matrix4x4::Identity(), projection) ||
			!DecalProjectionUtility::GetScreenRect(projection, camera, 100, 100, left, top, right, bottom) ||
			left != 0 || top != 0 || right != 100 || bottom != 100) return false;
		if (!DecalProjectionUtility::Build(component, Matrix4x4::MakeTranslateMatrix(Vector3(0, 0, 5)), projection) ||
			!DecalProjectionUtility::GetScreenRect(projection, camera, 100, 100, left, top, right, bottom) ||
			left <= 0 || right >= 100 || left >= right) return false;
		component.size.z = 0;
		if (DecalProjectionUtility::Build(component, parent, projection)) return false;
		component.size.z = std::numeric_limits<float>::quiet_NaN();
		return !DecalProjectionUtility::Build(component, parent, projection) &&
			Near(DecalProjectionUtility::DistanceFade(95, 100, 0.9f), 0.5f) &&
			Near(DecalProjectionUtility::DistanceFade(100, 100, 1), 0);
	}

	// 候補選別に使うBoundsが頂点を落とさない
	bool CheckBounds() {

		std::vector<MeshVertex> vertices(4);
		vertices[0].position = Vector4(-1, -2, 0, 1);
		vertices[1].position = Vector4(3, 2, 0, 1);
		vertices[2].position = Vector4(1, 0, 0, 1);
		vertices[3].position = Vector4(100, 100, 100, 1);
		const uint32_t indices[]{0, 1, 2};
		Vector3 center;
		float radius = 0;
		MeshBoundsUtility::Calculate(vertices, indices, center, radius);
		if (!Near(center.x, 1) || !Near(center.y, 0) || !Near(radius, std::sqrt(8.0f))) return false;
		center = {};
		radius = 1;
		MeshBoundsUtility::TransformSphere(Matrix4x4::MakeAffineMatrix(Vector3(-2, 3, 1),
			Vector3(0, 35, 0), Vector3(4, 1, 8)), center, radius);
		return Near(center.x, 4) && Near(center.y, 1) && Near(center.z, 8) && Near(radius, 3);
	}

	// Camera選別と重なり順は生成順や無効な投影に影響されない
	bool CheckCollection() {

		ECSWorld world;
		const auto first = SceneAuthoring::CreateGameObject(world, "First");
		const auto second = SceneAuthoring::CreateGameObject(world, "Second");
		world.AddComponent<DecalProjectorComponent>(first);
		world.AddComponent<DecalProjectorComponent>(second);
		world.GetComponent<DecalProjectorComponent>(first).sortOrder = 2;
		world.GetComponent<DecalProjectorComponent>(second).sortOrder = -1;
		ResolvedRenderView view;
		view.valid = view.perspective.valid = true;
		view.perspective.cullingMask = 1;
		SceneExecutionContext context;
		context.world = &world;
		context.view = &view;
		AssetDatabase database;
		database.Init();
		const auto defaultPath = RuntimePaths::GetEngineAssetsRoot() / "Rendering/Builtin/Decal/defaultDecal.material.json";
		const AssetID defaultMaterial = database.ImportOrGet(RuntimePaths::ToAssetPath(defaultPath), AssetType::Material);
		if (defaultMaterial != BuiltinAssets::Materials::DefaultDecal) return false;
		context.assetDatabase = &database;
		GraphicsCore graphics;
		RenderAssetLibrary assets;
		assets.Init(&database);
		// アセット化しても従来の初期パラメータを維持する
		const auto* defaults = assets.LoadMaterial(defaultMaterial);
		if (!defaults || ToJson(*defaults) != ToJson(CreateDefaultDecalMaterialAsset("DefaultDecal"))) return false;
		const auto revision = assets.GetMaterialRevision();
		auto material = CreateDefaultDecalMaterialAsset("Preview");
		material.guid = AssetID{1, 1};
		assets.RegisterDerivedMaterial(material);
		material.parameters.Set("opacity", MaterialParameterValue{.value = 0.5f});
		assets.RegisterDerivedMaterial(material);
		if (assets.GetMaterialRevision() != revision) return false;
		// Surfaceへの用途変更は既存の描画Cacheにも伝える
		material.domain = MaterialDomain::Surface;
		assets.RegisterDerivedMaterial(material);
		if (assets.GetMaterialRevision() == revision) return false;
		std::vector<DecalDrawItem> items;
		DecalCollector::Collect(graphics, assets, context, items);
		if (items.size() != 2 || items[0].entity != second || items[1].entity != first) return false;
		world.GetComponent<DecalProjectorComponent>(second).renderingLayerMask = 2;
		DecalCollector::Collect(graphics, assets, context, items);
		if (items.size() != 1 || items[0].entity != first) return false;
		world.GetComponent<SceneObjectComponent>(first).activeInHierarchy = false;
		DecalCollector::Collect(graphics, assets, context, items);
		return items.empty();
	}

	// 旧DecalのUVと乗算色を引き継ぎ、共有UVを持つ別Rendererの設定を維持する
	bool CheckDecalMigration() {

		ECSWorld world;
		AssetDatabase database;
		database.Init();
		const auto materialPath = RuntimePaths::GetEngineAssetsRoot() / "Rendering/Builtin/Decal/defaultDecal.material.json";
		const auto material = database.ImportOrGet(RuntimePaths::ToAssetPath(materialPath), AssetType::Material);
		enum class SharedUV { None, Existing, Renderer };
		for (const auto shared : {SharedUV::None, SharedUV::Existing, SharedUV::Renderer}) {
			const auto entity = SceneAuthoring::CreateGameObject(world, "Legacy");
			auto& old = world.AddComponent<DecalProjectorComponent>(entity);
			old.hasLegacyUV = true;
			old.uvScale = {2, -3};
			old.uvBias = {0.2f, -0.1f};
			old.uvRotation = 37;
			old.color = {0.5f, 1, 1, 0.5f};
			old.fadeFactor = 0.25f;
			old.material = material;
			old.materialInstance.Set("opacity", MaterialParameterValue{.value = 0.8f});
			old.materialInstance.Set("color", MaterialParameterValue{.value = Color4{0.8f, 1, 1, 1}});
			const auto expected = DecalProjectionUtility::LegacyUVMatrix(old);
			if (shared == SharedUV::Renderer) world.AddComponent<PrimitiveRendererComponent>(entity);
			if (shared == SharedUV::Existing) {
				auto& original = world.AddComponent<UVTransformComponent>(entity);
				original.scale = {0, 2};
				original.rotation = 17;
				original.uvMatrix = MakeUVMatrix(original);
			}
			MigrateDecalSettings(world, entity, &database);
			const auto& migrated = world.GetComponent<DecalProjectorComponent>(entity);
			const auto& uv = world.GetComponent<UVTransformComponent>(entity);
			if (shared == SharedUV::Renderer && uv.uvMatrix != Matrix4x4::Identity()) return false;
			if (shared == SharedUV::Existing && (uv.scale != Vector2(0, 2) || uv.rotation != 17)) return false;
			const auto result = DecalProjectionUtility::ResolveUVMatrix(migrated, &uv);
			for (int row : {0, 1, 3}) {
				if (!Near(result.m[row][0], expected.m[row][0]) || !Near(result.m[row][1], expected.m[row][1])) return false;
			}
			const auto* opacity = migrated.materialInstance.Find(MaterialParameterID::FromName("opacity"));
			const auto* color = migrated.materialInstance.Find(MaterialParameterID::FromName("color"));
			if (!opacity || !color || !Near(std::get<float>(opacity->value), 0.2f) || !Near(std::get<Color4>(color->value).r, 0.4f)) return false;
			const nlohmann::json saved = migrated;
			if (saved.contains("color") || saved.contains("uvScale") || saved.contains("fadeFactor")) return false;
			MigrateDecalSettings(world, entity, &database);
			if (nlohmann::json(world.GetComponent<DecalProjectorComponent>(entity)) != saved) return false;
		}
		return true;
	}

	// Scene複製とPrefab保存で投影と個別Materialを維持する
	bool CheckStorage() {

		ECSWorld world;
		const auto entity = SceneAuthoring::CreateGameObject(world, "Decal");
		world.AddComponent<DecalProjectorComponent>(entity);
		auto& component = world.GetComponent<DecalProjectorComponent>(entity);
		component.size = Vector3(3, 2, 0.2f);
		component.uvCorrectionU = {0.2f, -0.1f, 0.3f};
		component.materialInstance.Set("roughness", MaterialParameterValue{.value = 0.25f});
		nlohmann::json saved;
		if (!world.SerializeComponentToJson(entity, "DecalProjector", saved)) return false;
		auto cloned = world.CloneForSerialization();
		nlohmann::json copied;
		if (!cloned || !cloned->SerializeComponentToJson(entity, "DecalProjector", copied) || saved != copied) return false;
		const auto other = world.CreateEntity();
		if (!world.AddComponentFromJson(other, "DecalProjector", saved)) return false;
		nlohmann::json restored;
		if (!world.SerializeComponentToJson(other, "DecalProjector", restored) || saved != restored) return false;
		// C#の追加直後と同じ予約経路へ値を書き込む
		const auto pendingEntity = world.CreateEntity();
		world.GetCommandBuffer().StageAddComponent(world, pendingEntity,
			ComponentTypeRegistry::GetInstance().GetID<DecalProjectorComponent>());
		auto* pending = world.TryGetComponentForBinding<DecalProjectorComponent>(pendingEntity);
		if (!pending) return false;
		pending->size = Vector3(0.7f, 0.8f, 0.2f);
		pending->fadeFactor = 0.35f;
		pending->materialInstance.Set("opacity", MaterialParameterValue{.value = 0.75f});
		world.GetCommandBuffer().Flush(world);
		if (!Near(world.GetComponent<DecalProjectorComponent>(pendingEntity).fadeFactor, 0.35f) ||
			!Near(world.GetComponent<DecalProjectorComponent>(pendingEntity).size.x, 0.7f)) return false;
		const auto* opacity = world.GetComponent<DecalProjectorComponent>(pendingEntity).materialInstance.Find(
			MaterialParameterID::FromName("opacity"));
		if (!opacity || !std::holds_alternative<float>(opacity->value) || !Near(std::get<float>(opacity->value), 0.75f)) return false;
		const MaterialAsset material = CreateDefaultDecalMaterialAsset("Decal");
		MaterialAsset loaded;
		if (!FromJson(ToJson(material), loaded) || ToJson(material) != ToJson(loaded)) return false;
		MeshRendererComponent mesh;
		PrimitiveRendererComponent primitive;
		from_json(nlohmann::json::object(), mesh);
		from_json(nlohmann::json::object(), primitive);
		if (!mesh.receiveDecals || !primitive.receiveDecals) return false;
		NEMTests::TestDirectory directory("DecalPrefab", RuntimePaths::GetGameAssetsRoot());
		AssetDatabase database;
		database.Init();
		ProjectAssetFileResult result;
		if (!ProjectAssetOperations::SavePrefab(database, world, entity, ProjectAssetSource::Game,
			RuntimePaths::ToAssetPath(directory.GetPath()), result)) return false;
		const auto* asset = database.FindByPath(result.assetPath);
		PrefabSystem prefabs;
		HierarchySystem hierarchy;
		ECSWorld spawned;
		PrefabInstantiateResult instance;
		if (!asset || !prefabs.InstantiatePrefab(database, hierarchy, spawned, asset->guid, instance) ||
			instance.createdEntities.size() != 1) return false;
		nlohmann::json prefab;
		return spawned.SerializeComponentToJson(instance.createdEntities.front(), "DecalProjector", prefab) && prefab == saved;
	}
}

bool NEMTests::TestDecals() {

	return RunTest("DecalGraphMaterial", CheckGraphMaterial) && RunTest("DecalProjection", CheckProjection) && RunTest("DecalBounds", CheckBounds) &&
		RunTest("DecalCollection", CheckCollection) && RunTest("DecalStorage", CheckStorage) && RunTest("DecalMigration", CheckDecalMigration);
}
