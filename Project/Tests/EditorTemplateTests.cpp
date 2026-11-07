#include "EditorTemplateTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Templates/EditorTemplateSnapshot.h>
#include <Engine/Editor/Templates/EditorTemplateStorage.h>
#include <Engine/Editor/Assets/Project/EditorSceneDefaults.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>

// c++
#include <fstream>
#include <iostream>

namespace {

	// 単一三角形と外部Bufferを持つglTFを作成する
	bool WriteModel(const std::filesystem::path& root) {

		const float positions[]{0, 0, 0, 1, 0, 0, 0, 1, 0};
		const uint16_t indices[]{0, 1, 2};
		std::ofstream buffer(root / "triangle.bin", std::ios::binary);
		buffer.write(reinterpret_cast<const char*>(positions), sizeof(positions));
		buffer.write(reinterpret_cast<const char*>(indices), sizeof(indices));
		buffer.close();
		const auto data = nlohmann::json::parse(R"({
			"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
			"buffers":[{"uri":"triangle.bin","byteLength":42}],
			"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],
			"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
			{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
			"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}]
		})");
		return Engine::JsonFile::Save(root / "triangle.gltf", data);
	}
}

bool NEMTests::TestEditorTemplates() {

	using namespace Engine;
	TestDirectory source("EditorTemplates", RuntimePaths::GetGameAssetsRoot());
	const auto scenePath = source.GetPath() / "Template.scene.json";
	const AssetID scene = AssetID::New();
	const AssetID model = AssetID::New();
	const AssetID texture = AssetID::New();
	const AssetID unused = AssetID::New();
	const auto sceneLogical = RuntimePaths::ToAssetPath(scenePath);
	auto meta = [](AssetID id, AssetType type, const std::filesystem::path& path) {
		AssetMeta result;
		result.guid = id;
		result.type = type;
		result.assetPath = RuntimePaths::ToAssetPath(path);
		return AssetDatabase::WriteMetaFile(std::filesystem::path(path.native() + L".meta"), result);
	};
	std::ofstream(source.GetPath() / "used.png") << "used";
	std::ofstream(source.GetPath() / "unused.png") << "unused";
	std::ofstream(source.GetPath() / "dynamic.csv") << "name,value\ncheck,1\n";
	std::ofstream(source.GetPath() / "Helper.cs") << "internal static class TemplateHelper {}\n";
	if (!WriteModel(source.GetPath()) || !meta(model, AssetType::Mesh, source.GetPath() / "triangle.gltf") ||
		!meta(texture, AssetType::Texture, source.GetPath() / "used.png") ||
		!meta(unused, AssetType::Texture, source.GetPath() / "unused.png") || !meta(scene, AssetType::Scene, scenePath)) return false;
	auto document = EditorSceneDefaults::MakeScene("Template test");
	document["Entities"] = nlohmann::json::array();
	document["TemplateAssetReferences"] = {ToString(model), ToString(texture)};
	if (!JsonFile::Save(scenePath, document)) return false;
	AssetDatabase database;
	if (!database.Init() || !database.RebuildMeta()) return false;
	auto entry = EditorTemplateStorage::MakeEntry("Sample");
	entry.name = "日本語のテンプレート";
	entry.description = "依存収集の確認";
	entry.startupScene = scene;
	entry.additionalPaths = {RuntimePaths::ToAssetPath(source.GetPath() / "dynamic.csv")};
	const auto target = EditorTemplateStorage::ResolveRoot(entry.id);
	ScopedCleanup cleanup([&]() noexcept {
		std::error_code error;
		std::filesystem::remove_all(target, error);
	});
	EditorTemplateSaveRequest request{entry, EditorTemplateStorage::Repository(), RuntimePaths::GetProjectRoot()};
	auto result = SaveEditorTemplate(request, database);
	if (!result.success) { std::cerr << result.message << '\n'; return false; }
	const auto relative = source.GetPath().lexically_relative(RuntimePaths::GetProjectRoot());
	const auto copied = target / relative;
	if (!std::filesystem::exists(copied / "triangle.bin") || !std::filesystem::exists(copied / "used.png.meta") ||
		!std::filesystem::exists(copied / "Helper.cs") || !std::filesystem::exists(copied / "dynamic.csv") ||
		std::filesystem::exists(copied / "unused.png") || std::filesystem::exists(target / "Library") ||
		std::filesystem::exists(target / "Saved")) {
		std::cerr << "Snapshot files differ: " << copied << '\n';
		for (const auto& item : std::filesystem::recursive_directory_iterator(copied)) std::cerr << item.path() << '\n';
		return false;
	}
	nlohmann::json startup, saved;
	if (!JsonFile::TryLoad(target / "ProjectSettings/Runtime/StartupScene.json", startup) ||
		startup.value("activeScene", std::string{}) != ToString(scene) || !JsonFile::TryLoad(target / "template.json", saved) ||
		saved.value("name", std::string{}) != entry.name) { std::cerr << "Snapshot startup scene or name differs\n"; return false; }
	std::ifstream project(target / "Scripts/GameScripts.csproj");
	const std::string projectText((std::istreambuf_iterator<char>(project)), std::istreambuf_iterator<char>());
	project.close();
	if (projectText.find("ProjectReference") != std::string::npos || projectText.find("NEMEngineRoot") == std::string::npos) {
		std::cerr << "Snapshot C# project is not portable\n";
		return false;
	}
	const auto before = ContentHash::FileSHA256(target / "template.json");
	request.entry.additionalPaths.push_back("../outside.txt");
	if (SaveEditorTemplate(request, database).success || ContentHash::FileSHA256(target / "template.json") != before) {
		std::cerr << "Invalid additional path changed snapshot\n";
		return false;
	}
	request.entry.additionalPaths.pop_back();
	const Engine::UUID actor = Engine::UUID::New();
	const auto actorRoot = SceneAssetStorage::ResolveActorRoot(scenePath, scene);
	std::filesystem::create_directories(actorRoot);
	ScopedCleanup actorCleanup([&]() noexcept { std::error_code error; std::filesystem::remove_all(actorRoot, error); });
	if (!JsonFile::Save(actorRoot / (ToString(actor) + ".actor.json"), nlohmann::json{
		{"SchemaVersion", 1}, {"LocalFileID", ToString(actor)}, {"Components", nlohmann::json::object()}})) return false;
	document.erase("Entities");
	document["ExternalActors"] = {ToString(actor)};
	if (!JsonFile::Save(scenePath, document)) return false;
	result = SaveEditorTemplate(request, database);
	if (!result.success || !JsonFile::TryLoad(target / Algorithm::PathFromUTF8(sceneLogical), saved)) {
		std::cerr << "External actor snapshot failed: " << result.message << '\n';
		return false;
	}
	return !saved.contains("ExternalActors") && saved["Entities"].size() == 1 &&
		saved["Entities"][0].value("LocalFileID", std::string{}) == ToString(actor);
}
