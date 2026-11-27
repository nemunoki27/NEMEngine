#include "EditorAssetWorkflowTests.h"
#include "TestFixtures.h"

#include <Engine/Core/Assets/Database/AssetDependencyScanner.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Database/AssetMetaStorage.h>
#include <Engine/Core/Assets/Watch/PublishedAssetChanges.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Rendering/Materials/PaintMaterialAsset.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphParameterDefaults.h>
#include <Engine/Core/Rendering/Materials/MaterialPaintAtlas.h>
#include <Engine/Core/Rendering/Materials/MaterialPaintMask.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/World/Components/Rendering/MaterialPaintComponent.h>

#include <algorithm>

namespace {

	bool CheckAtlasPadding() {

		using namespace Engine;
		constexpr int side = 512;
		MaterialPaintAtlas atlas;
		auto& triangle = atlas.triangles.emplace_back();
		triangle.uv = {Vector2(0.25f, 0.25f), Vector2(0.75f, 0.25f), Vector2(0.25f, 0.75f)};
		const std::array<uint8_t, 4> color = {12, 90, 165, 230};
		std::vector<uint8_t> pixels(side * side * 4);
		const auto pixel = [&](int x, int y) { return pixels.data() + (y * side + x) * 4; };
		for (int y = 128; y < 384; ++y) {
			for (int x = 128; x + y < 512; ++x) std::copy(color.begin(), color.end(), pixel(x, y));
		}
		DilateMaterialPaintMask(atlas, side, pixels);
		for (int x = 124; x <= 128; ++x) {
			if (!std::equal(color.begin(), color.end(), pixel(x, 150))) return false;
		}
		const std::array<uint8_t, 4> empty{};
		return std::equal(empty.begin(), empty.end(), pixel(123, 150));
	}

	bool CheckPrefabPaint() {

		using namespace Engine;
		NEMTests::TestDirectory directory("PaintPrefab");
		const auto& root = directory.GetPath();
		std::filesystem::create_directories(root / "GameAssets");
		if (!JsonFile::Save(root / "Paint.nemproject", {{"schemaVersion", 1}, {"projectGuid", ToString(AssetID::New())},
			{"name", "Paint"}, {"assetsDirectory", "GameAssets"}, {"packagesDirectory", "Packages"},
			{"projectSettingsDirectory", "ProjectSettings"}, {"sceneStorage", "Monolithic"}})) return false;
		const auto previous = std::filesystem::current_path();
		ScopedCleanup restore([&]() noexcept {
			std::error_code error;
			std::filesystem::current_path(previous, error);
			try { RuntimePaths::Refresh(); } catch (...) {}
		});
		const auto previousLogger = spdlog::default_logger();
		const bool ownsLogger = !Logger::Get(LogType::Engine);
		if (ownsLogger) Logger::CreateLogFiles(root / "Logs");
		ScopedCleanup closeLog([&]() noexcept {
			if (ownsLogger) { Logger::Finalize(); spdlog::set_default_logger(previousLogger); }
		});
		std::filesystem::current_path(root);
		RuntimePaths::Refresh();
		if (RuntimePaths::GetGameAssetsRoot() != root / "GameAssets") return false;
		AssetDatabase database;
		if (!database.Init()) return false;
		std::vector<uint8_t> pixels(512 * 512 * 4);
		pixels[0] = 255;
		pixels[1] = 127;
		pixels[5] = 19;
		pixels[7] = 241;
		const auto mask = SaveMaterialPaintMask(database, 512, pixels);
		std::vector<uint8_t> readback;
		if (!mask || !LoadMaterialPaintMask(database, mask, 512, readback) || readback != pixels ||
			SaveMaterialPaintMask(database, 512, pixels) != mask) return false;
		const auto materialPath = root / "GameAssets/Stone.paintmaterial";
		PaintMaterialAsset originalPaint;
		originalPaint.channels = 21;
		originalPaint.baseColor = Color4(0.6f, 0.3f, 0.1f, 1.0f);
		originalPaint.tiling = 0.16f;
		originalPaint.normalStrength = 0.7f;
		originalPaint.heightTexture = mask;
		originalPaint.normalTexture = mask;
		if (!JsonFile::SaveCanonical(materialPath, ToJson(originalPaint), 2)) return false;
		const AssetID material = database.ImportOrGet(RuntimePaths::ToAssetPath(materialPath), AssetType::PaintMaterial);
		if (!material) return false;
		const auto occupied = root / "GameAssets/Stone.material";
		if (!JsonFile::SaveCanonical(occupied, ToJson(CreateDefaultMeshMaterialAsset("Existing")), 2) ||
			!database.RebuildMeta({root / "GameAssets"})) return false;
		const auto* migrated = database.Find(material);
		PaintMaterialAsset sample;
		if (!migrated || migrated->type != AssetType::Material || migrated->assetPath != "GameAssets/Stone_Paint1.material" ||
			std::filesystem::exists(materialPath) || std::filesystem::exists(AssetMetaStorage::MetaPathOf(materialPath)) ||
			!LoadPaintMaterialAsset(database.ResolveFullPath(material), sample) || sample != originalPaint) return false;
		ECSWorld source;
		const auto object = SceneAuthoring::CreateGameObject(source, "Stone");
		auto& original = source.AddComponent<MaterialPaintComponent>(object);
		const auto atlas = std::make_shared<MaterialPaintAtlas>();
		auto editedPixels = pixels;
		editedPixels[7] = 97;
		const auto queuedMask = QueueMaterialPaintMask(database, 512, std::move(editedPixels), atlas);
		if (!queuedMask || !LoadMaterialPaintMask(database, queuedMask, 512, readback) || readback[7] != 97) return false;
		original.mask = queuedMask;
		original.layers.push_back({material, 0});
		const std::string prefabPath = "GameAssets/Stone.prefab.json";
		PrefabSystem prefabs;
		if (!prefabs.SavePrefab(database, source, object, prefabPath) || !database.Find(queuedMask)) return false;

		AssetDatabase reopened;
		if (!reopened.Init() || !reopened.RebuildMeta({root / "GameAssets"}) ||
			!LoadMaterialPaintMask(reopened, queuedMask, 512, readback) || readback[7] != 97) return false;
		HierarchySystem hierarchy;
		ECSWorld scene;
		PrefabInstantiateResult first{}, second{};
		if (!prefabs.InstantiatePrefabFromPath(database, hierarchy, scene, prefabPath, first) ||
			!prefabs.InstantiatePrefabFromPath(database, hierarchy, scene, prefabPath, second)) return false;
		auto& paint = scene.GetComponent<MaterialPaintComponent>(first.root);
		if (paint.layers.size() != 1 || paint.layers.front().material != material) return false;
		pixels[0] = 0;
		const auto revision = database.GetStructureRevision();
		paint.mask = QueueMaterialPaintMask(database, 512, std::move(pixels), atlas);
		std::vector<AssetID> additions;
		if (!PublishMaterialPaintMasks(database, true) || !PublishedAssetChanges::CollectAdditions(database, revision, additions) ||
			additions != std::vector{paint.mask}) return false;
		if (!paint.mask || paint.mask == mask) return false;
		const auto maskPath = database.ResolveFullPath(paint.mask);
		if (!PublishedAssetChanges::Matches(database, maskPath) ||
			!PublishedAssetChanges::Matches(database, AssetMetaStorage::MetaPathOf(maskPath))) return false;
		paint.layers.front().visible = false;
		scene.MarkComponentModified<MaterialPaintComponent>(first.root);
		const auto& other = scene.GetComponent<MaterialPaintComponent>(second.root);
		return other.mask == queuedMask && original.mask == queuedMask && other.layers.size() == 1 && other.layers.front().visible && original.layers.front().visible;
	}
}

bool NEMTests::TestMaterialPaint() {

	using namespace Engine;
	TestDirectory directory("MaterialPaint");
	const auto path = directory.GetPath() / "Mud.paintmaterial";
	PaintMaterialAsset material;
	material.channels = 5;
	material.baseColor = Color4(0.2f, 0.3f, 0.4f, 1.0f);
	material.roughnessTexture = AssetID::New();
	material.tiling = 1.75f;
	if (AssetTypeResolver::GuessByPath(path) != AssetType::PaintMaterial ||
		!AssetTypeResolver::IsJsonAssetFile(AssetType::PaintMaterial, path) ||
		!JsonFile::SaveCanonical(path, ToJson(material), 2)) return false;
	PaintMaterialAsset restored;
	if (!LoadPaintMaterialAsset(path, restored) || restored != material) return false;
	const auto previous = ConvertLegacyPaintMaterial("Mud", restored);
	auto regenerated = CreateDefaultMeshMaterialAsset("Mud");
	ShaderGraphParameterDefaults::RestoreOverrides({}, previous, regenerated);
	const auto sample = MakePaintMaterialSample(regenerated);
	if (sample.channels != material.channels || sample.tiling != material.tiling) return false;

	MaterialPaintComponent paint;
	paint.resolution = 512;
	paint.atlas = AssetID::New();
	paint.geometryKey = "cube";
	paint.mask = AssetID::New();
	paint.layers.push_back({AssetID::New(), 2, true, true, 3.0f});
	paint.baseHeightTextures.push_back(AssetID::New());
	paint.projectionFromLocal.m[3][0] = 5.5f;
	const nlohmann::json document = paint;
	const auto decoded = document.get<MaterialPaintComponent>();
	if (nlohmann::json(decoded) != document) return false;
	AssetDependencyScanner::IDReferences ids;
	AssetDependencyScanner::PathReferences paths;
	AssetDependencyScanner::ScanReferences(document, ids, paths);
	const auto references = [&](AssetID id, AssetType type) {
		const auto found = ids.find(id);
		return found != ids.end() && found->second == type;
	};
	return CheckAtlasPadding() && CheckPrefabPaint() && ids.size() == 4 && references(paint.atlas, AssetType::PaintAtlas) && references(paint.mask, AssetType::Texture) &&
		references(paint.layers.front().material, AssetType::Material) && references(paint.baseHeightTextures.front(), AssetType::Texture);
}
