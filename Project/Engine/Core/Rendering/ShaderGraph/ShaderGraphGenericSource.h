#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphAsset.h"

namespace Engine::ShaderGraphStageSource {

	// 汎用ノードの共有型を生成する
	std::string BuildGenericTypes();
	// 汎用ノードの共有関数を生成する
	std::string BuildGenericFunctions(const ShaderGraphAsset& graph, bool bindlessTextures);
	// 色と法線の共有関数を生成する
	std::string BuildGenericColorFunctions();
	// 図形とノイズの共有関数を生成する
	std::string BuildGenericProceduralFunctions();
	// テクスチャを使う共有関数を生成する
	std::string BuildGenericTextureFunctions(const ShaderGraphAsset& graph, bool bindlessTextures);
}
