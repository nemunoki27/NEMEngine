#include "RenderPathGraphSerializer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphSerializer.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <filesystem>

//============================================================================
//	RenderPathGraphSerializer classMethods
//============================================================================

std::string Engine::RenderPathGraphSerializer::MakeDefaultGraphPath(const std::string& scenePath) {

	// sample.scene.json のようなScene名からGraph保存名を作る
	std::filesystem::path scene = scenePath;
	std::string fileName = scene.stem().string();
	if (std::filesystem::path(fileName).extension() == ".scene") {
		fileName = std::filesystem::path(fileName).stem().string();
	}
	if (fileName.empty()) {
		fileName = "UntitledScene";
	}

	// RenderPath GraphはEngine Asset配下の専用フォルダへ保存する
	std::filesystem::path graphPath = RuntimePaths::GetEngineAssetPath("Graphs/RenderPath");
	graphPath /= fileName + ".renderpath.graph.json";
	return graphPath.generic_string();
}

bool Engine::RenderPathGraphSerializer::ExportGraph(
	const std::string& path, const GraphDocument& document, std::string* error) {

	// 実際のJSON変換は汎用GraphSerializerへ任せる
	return GraphSerializer::Save(path, document, error);
}

bool Engine::RenderPathGraphSerializer::ImportGraph(
	const std::string& path, GraphDocument& document, std::string* error) {

	// RenderPath専用形式も中身はGraphDocumentなので共通Serializerを使う
	return GraphSerializer::Load(path, document, error);
}
