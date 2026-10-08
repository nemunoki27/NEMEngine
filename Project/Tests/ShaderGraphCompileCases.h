#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>

namespace NEMTests {

	class ShaderGraphCompileFixture;

	// 設定取り込みと失敗時の旧値維持を確認する
	bool TestShaderGraphSettingsImport(const ShaderGraphCompileFixture& fixture);

	// 既定Graphと各実行領域の生成を確認する
	bool TestShaderGraphDefaultDomains(const ShaderGraphCompileFixture& fixture, const Engine::ShaderGraphAsset& graph);

	// 描画対象ごとのShaderと保存値を確認する
	bool TestShaderGraphRasterTargets(const ShaderGraphCompileFixture& fixture);

	// Shaderの入口と公開Parameterの構成を確認する
	bool TestShaderGraphShaderDefinitions();

	// 静的Samplerの登録と保存値を確認する
	bool TestShaderGraphStaticSamplers(const ShaderGraphCompileFixture& fixture);

	// 頂点変形のVSとMSを確認する
	bool TestShaderGraphVertexTargets(const ShaderGraphCompileFixture& fixture);

	// 演算ノードの既定値と保存値を確認する
	bool TestShaderGraphNodeDefaults();

	// ディザの全描画経路と実際のGPU転送配置を確認する
	bool TestShaderGraphDither(const ShaderGraphCompileFixture& fixture);

	// 時刻ノードの全出力を確認する
	bool TestShaderGraphTimeOutputs();

	// SubGraph展開と重複IDの拒否を確認する
	bool TestShaderGraphSubGraphs(Engine::ShaderGraphAsset& graph);
} // NEMTests
