#include "ShaderGraphNodePreviewCases.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodeRegistry.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

	using namespace Engine;
	using namespace NEMTests;

	// 検証Project専用のJSONを書き込む
	bool WriteJson(const std::filesystem::path& path, const nlohmann::json& data) {

		std::filesystem::create_directories(path.parent_path());
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		stream << data.dump(2) << '\n';
		return stream.good();
	}

	// Importerが参照する検証用GUIDを固定する
	bool WriteAsset(const std::filesystem::path& path, const nlohmann::json& data, AssetID id,
		const char* type, const char* importer) {

		return WriteJson(path, data) && WriteJson(path.string() + ".meta", {
			{"guid", ToString(id)}, {"type", type}, {"schemaVersion", 2},
			{"importer", importer}, {"importerVersion", 1}, {"settings", nlohmann::json::object()},
		});
	}

	// 操作時に全Nodeが収まる4個ずつのページへ分ける
	ShaderGraphAsset CreatePage(uint32_t index) {

		ShaderGraphAsset graph;
		graph.name = "PreviewPage" + std::to_string(index);
		graph.surfaceMode = ShaderGraphSurfaceMode::Transparent;
		graph.outputNode = Engine::UUID::New();
		graph.nodes.emplace_back(ShaderGraphNode{
			.id = graph.outputNode, .kind = ShaderGraphNodeKind::SurfaceOutput,
			.position = {800, 70}, .previewExpanded = false,
		});
		return graph;
	}
}

bool NEMTests::WriteShaderGraphNodePreviewFixtures() {

	const auto root = RuntimePaths::GetEngineProjectRoot().parent_path() / "LocalWork/Verification/ShaderGraphVerification";
	const auto assets = root / "GameAssets";
	const AssetID sceneID{71, 1};
	if (!WriteJson(root / "ShaderGraphVerification.nemproject", {
		{"schemaVersion", 1}, {"projectGuid", "7a000000000000000000000000000001"},
		{"name", "ShaderGraphVerification"}, {"sceneStorage", "Monolithic"},
		{"assetsDirectory", "GameAssets"}, {"packagesDirectory", "Packages"},
		{"projectSettingsDirectory", "ProjectSettings"},
	}) || !WriteAsset(assets / "Scenes/Main.scene.json", {
		{"SchemaVersion", 3}, {"Header", {{"name", "ShaderGraphVerification"}, {"subScenes", nlohmann::json::array()}}},
		{"Entities", nlohmann::json::array()}, {"PrefabInstances", nlohmann::json::array()},
	}, sceneID, "Scene", "SceneImporter") || !WriteJson(root / "ProjectSettings/Runtime/StartupScene.json", {
		{"activeScene", ToString(sceneID)},
	})) return false;
	if (!WriteAsset(assets / "Previews/PreviewSubGraph.shadergraph.json", ToJson(CreateShaderGraphPreviewSubGraph()),
		{71, 10}, "ShaderGraph", "ShaderGraphImporter")) return false;

	nlohmann::json catalog = nlohmann::json::array();
	uint32_t pageIndex = 0, pageCount = 0;
	ShaderGraphAsset page = CreatePage(pageIndex);
	ShaderGraphAsset gallery = CreatePage(0);
	gallery.name = "All Nodes";
	gallery.target = ShaderGraphTarget::Particle;
	gallery.nodes.front().kind = ShaderGraphNodeKind::UnlitOutput;
	gallery.nodes.front().position = {1650, 70};
	uint32_t galleryCount = 0;
	const auto writePage = [&]() {
		std::ostringstream name;
		name << "Page" << std::setfill('0') << std::setw(2) << pageIndex << ".shadergraph.json";
		return WriteAsset(assets / "Previews" / name.str(), ToJson(page), {71, 100 + pageIndex},
			"ShaderGraph", "ShaderGraphImporter");
	};
	for (const auto& descriptor : ShaderGraphNodeRegistry::GetDescriptors()) {
		auto testCase = CreateShaderGraphNodePreviewCase(descriptor.kind);
		auto& node = testCase.graph.nodes.front();
		nlohmann::json entry{
			{"kind", ToJson(testCase.graph)["nodes"][0]["kind"]}, {"name", descriptor.name},
			{"category", descriptor.category}, {"preview", descriptor.preview},
			{"outputCount", GetShaderGraphOutputCount(node)},
		};
		if (descriptor.preview) {
			if (pageCount == 4) {
				if (!writePage()) return false;
				page = CreatePage(++pageIndex);
				pageCount = 0;
			}
			node.position = Vector2(50.0f + float(pageCount % 2) * 360.0f, 70.0f + float(pageCount / 2) * 520.0f);
			node.previewExpanded = true;
			page.nodes.emplace_back(node);
			ShaderGraphNode galleryNode = node;
			galleryNode.position = Vector2(50.0f + float(galleryCount % 4) * 380.0f, 70.0f + float(galleryCount / 4) * 420.0f);
			gallery.nodes.emplace_back(std::move(galleryNode));
			gallery.parameters.insert(gallery.parameters.end(), testCase.graph.parameters.begin(), testCase.graph.parameters.end());
			gallery.keywords.insert(gallery.keywords.end(), testCase.graph.keywords.begin(), testCase.graph.keywords.end());
			++galleryCount;
			page.parameters.insert(page.parameters.end(), testCase.graph.parameters.begin(), testCase.graph.parameters.end());
			page.keywords.insert(page.keywords.end(), testCase.graph.keywords.begin(), testCase.graph.keywords.end());
			if (descriptor.kind == ShaderGraphNodeKind::ParticleData) {
				page.target = ShaderGraphTarget::Particle;
				page.nodes.front().kind = ShaderGraphNodeKind::UnlitOutput;
			}
			entry["page"] = pageIndex;
			entry["nodeID"] = ToString(node.id);
			++pageCount;
		}
		catalog.push_back(std::move(entry));
	}
	if (pageCount && !writePage()) return false;
	if (!WriteAsset(assets / "Previews/ALL_Nodes.shadergraph.json", ToJson(gallery), {71, 99},
		"ShaderGraph", "ShaderGraphImporter")) return false;
	if (!WriteJson(root / "PreviewCatalog.json", catalog)) return false;
	std::cout << "Shader Graph verification project: " << root << " pages=" << pageIndex + 1 << '\n';
	return true;
}
