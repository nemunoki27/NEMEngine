#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphCompiler.h"

namespace Engine {

	class AssetDatabase;

	namespace ShaderGraphAssetReferences {

		// 外部関数の参照をコンパイルで使うパスへ解決する
		ShaderGraphAsset ResolveFunctions(const ShaderGraphAsset& graph, const AssetDatabase* database);
		// 子Graphと外部関数の参照を読み込む
		bool Load(AssetID id, const AssetDatabase* database, ShaderGraphAsset& graph);
		// グラフの元文書を読み込む
		bool LoadSource(AssetID id, const AssetDatabase* database, ShaderGraphAsset& graph);
		// 子Graphの公開入力と出力からピン情報を更新する
		bool RefreshSubGraphPorts(ShaderGraphNode& node, const ShaderGraphAsset& child,
			const ShaderGraphAssetResolver& resolver = {});
		// 子Graphの変更に合わせてピンと接続番号を更新する
		bool RefreshSubGraphs(ShaderGraphAsset& graph, const AssetDatabase* database);
	}
} // Engine
