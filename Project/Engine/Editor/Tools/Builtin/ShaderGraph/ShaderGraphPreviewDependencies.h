#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>

namespace Engine::ShaderGraphPreviewDependencies {

	// 外部関数のAssetIDをコンパイルで使う絶対パスへ解決する
	ShaderGraphAsset ResolveFunctions(const ShaderGraphAsset& graph, const AssetDatabase* database);
	// 参照Graphを読み込んで外部関数も解決する
	bool LoadGraph(AssetID id, const AssetDatabase* database, ShaderGraphAsset& graph);
	// 子Graphと関数ファイルを含めた変更を識別する
	uint64_t CalculateHash(const ShaderGraphAsset& graph, const AssetDatabase* database);
} // Engine::ShaderGraphPreviewDependencies
