#include "BakedLightingTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Lighting/Baked/BakedLightingGeometry.h>
#include <Engine/Core/Rendering/Lighting/Baked/BakedLightingInput.h>
#include <Engine/Core/Rendering/Lighting/Baked/BakedLightingStorage.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <atomic>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace NEMTests {

	// UVが重なる元モデルから別のAtlasを生成する
	Engine::BakedLightingInput MakeBakedLightingTestInput(const std::filesystem::path& directory) {

		using namespace Engine;
		const auto path = directory / "lighting.obj";
		std::ofstream stream(path);
		stream << "v -1 0 -1\nv -1 0 1\nv 1 0 1\nv 1 0 -1\nv -1 2 1\nv 1 2 1\n"
			"vt 0 0\nvt 0 1\nvt 1 1\nvt 1 0\nvn 0 1 0\nvn 0 0 -1\n"
			"o Floor\nf 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n"
			"o EmissiveWall\nf 2/1/2 5/2/2 6/3/2\nf 2/1/2 6/3/2 3/4/2\n";
		stream.close();
		BakedLightingInput input;
		input.preparation.data.receivers.push_back({Engine::UUID::New(), Matrix4x4::Identity(), "{}"});
		BakedLightingMeshInput mesh;
		mesh.mesh = AssetID{0x42414b4554455354ull, 1};
		mesh.path = path;
		mesh.worldMatrix = Matrix4x4::Identity();
		mesh.subMeshes.resize(2);
		mesh.materials = {0, 1};
		input.preparation.meshes.push_back(std::move(mesh));
		BakedLightingMaterialInput floor;
		floor.material = CreateDefaultMeshMaterialAsset("BakeFloor");
		floor.material.parameters.Set(MaterialParameterNames::BaseColor, MaterialParameterValue{Color4::White()});
		BakedLightingMaterialInput wall = floor;
		wall.material.parameters.Set(MaterialParameterNames::BaseColor, MaterialParameterValue{Color4(0.8f, 0.02f, 0.01f, 1.0f)});
		wall.material.parameters.Set(MaterialParameterNames::EmissiveColor, MaterialParameterValue{Color4(3.0f, 0.05f, 0.01f, 1.0f)});
		wall.material.parameters.Set(MaterialParameterNames::EmissiveIntensity, MaterialParameterValue{1.0f});
		input.preparation.materials = {floor, wall};
		input.preparation.data.inputRevisions["lightingState"] = nlohmann::json::object();
		return input;
	}
}

bool NEMTests::TestBakedLightingCPU() {

	using namespace Engine;
	TestDirectory directory("BakedLighting", RuntimePaths::GetGameAssetsRoot());
	auto input = MakeBakedLightingTestInput(directory.GetPath());
	std::atomic<bool> cancelled = false;
	std::atomic<float> progress = 0.0f;
	BakedLightingSettings settings;
	settings.atlasSize = 64;
	BakedLightingGeometry::Prepare(input.preparation, settings, cancelled, progress);
	auto& data = input.preparation.data;
	if (data.triangles.size() != 4 || data.nodes.empty() || data.atlasSize != 64 || data.atlasCount == 0) return false;
	for (const auto& triangle : data.triangles) {
		for (const auto& vertex : triangle.vertices) {
			if (!std::isfinite(vertex.lightmapU) || vertex.lightmapU < 0.0f || vertex.lightmapU > 1.0f ||
				vertex.lightmapV < 0.0f || vertex.lightmapV > 1.0f) return false;
		}
	}
	data.lightmaps.resize(input.preparation.texels.size(), Vector4(2.0f, 0.5f, 0.25f, 1.0f));
	BakedLightingGeometry::FilterLightmaps(data, input.preparation.texels, cancelled);
	for (size_t index = 0; index < input.preparation.texels.size(); ++index) {
		if (input.preparation.texels[index].valid && std::abs(data.lightmaps[index].x - 2.0f) > 1e-5f) return false;
	}
	AssetDatabase database;
	database.Init();
	const AssetID mesh = database.ImportOrGet(RuntimePaths::ToAssetPath(directory.GetPath() / "lighting.obj"), AssetType::Mesh);
	const auto metadata = *database.Find(mesh);
	data.inputRevisions["assets"] = nlohmann::json::array({{{"assetId", ToAssetReferenceJson(mesh)},
		{"hash", ContentHash::FileSHA256(database.ResolveFullPath(mesh))}, {"importer", metadata.importer},
		{"importerVersion", metadata.importerVersion}, {"importerSettings", metadata.importerSettings}}});
	BakedLightingInputBuilder::ValidateAssets(data, database, true);
	// 本体が同じでもインポート設定の変更で旧結果を失効させる
	const auto changedSettings = nlohmann::json{{"scale", 2.0f}};
	if (!database.UpdateImporterSettings(mesh, changedSettings, metadata.importerVersion)) return false;
	bool rejected = false;
	try { BakedLightingInputBuilder::ValidateAssets(data, database, false); }
	catch (const std::runtime_error&) { rejected = true; }
	if (!rejected || !database.UpdateImporterSettings(mesh, metadata.importerSettings, metadata.importerVersion)) return false;
	std::string diagnostic;
	const auto path = RuntimePaths::ToAssetPath(directory.GetPath() / "result.bakedlighting.json");
	const AssetID asset = BakedLightingStorage::Save(database, path, data, diagnostic);
	if (!asset) { std::cerr << diagnostic << '\n'; return false; }
	const auto document = JsonFile::Load(database.ResolveFullPath(asset), false);
	const auto loaded = BakedLightingStorage::Load(database.ResolveFullPath(asset),
		database.ResolveFullPath(ParseAssetID(document, "bakedLightingData")));
	// 範囲外参照を持つ結果では旧Assetを上書きしない
	const auto originalBytes = JsonFile::Load(database.ResolveFullPath(asset), false);
	data.nodes.front().left = static_cast<uint32_t>(data.nodes.size() + 1);
	if (BakedLightingStorage::Save(database, path, data, diagnostic) || diagnostic.empty() ||
		JsonFile::Load(database.ResolveFullPath(asset), false) != originalBytes) return false;
	return loaded.lightmaps == data.lightmaps && loaded.triangles.size() == data.triangles.size() &&
		loaded.nodes.size() == data.nodes.size() && loaded.triangleOrder == data.triangleOrder;
}
