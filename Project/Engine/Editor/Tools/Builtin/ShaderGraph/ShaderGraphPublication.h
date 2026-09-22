#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphIR.h>

#include <filesystem>

namespace Engine::ShaderGraphPublication {

	// 派生Assetの基準名を取得する
	std::string GraphFileStem(const std::filesystem::path& path);
	// コンパイル結果を保存して描画側へ公開する
	bool CompileAndPublish(const EditorToolContext& context, AssetDatabase& database, const ShaderGraphAsset& graph,
		AssetID assetID, const std::filesystem::path& graphPath, AssetID& materialID,
		std::vector<ShaderGraphDiagnostic>& diagnostics, std::string& status);
}
