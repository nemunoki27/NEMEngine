#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodePreviewCompiler.h>

namespace NEMTests {

	class ShaderGraphNodePreviewGPUFixture;

	struct ShaderGraphNodePreviewCase {

		Engine::ShaderGraphAsset graph;
		Engine::UUID nodeID{};
		Engine::ShaderGraphAssetResolver resolver;
	};

	// 登録済みNodeの必須参照を持つ検証Graphを作る
	ShaderGraphNodePreviewCase CreateShaderGraphNodePreviewCase(Engine::ShaderGraphNodeKind kind);
	// 実TextureとSamplerを接続した検証Graphを作る
	ShaderGraphNodePreviewCase CreateShaderGraphNodePreviewTextureCase(Engine::Vector2 uv,
		D3D12_TEXTURE_ADDRESS_MODE mode, D3D12_FILTER filter);
	// 値の型を保った入力を検証Graphへ接続する
	void ConnectShaderGraphPreviewValue(ShaderGraphNodePreviewCase& testCase, uint32_t inputSlot,
		Engine::ShaderGraphValueType type, const Engine::MaterialParameterValue& value);
	// 未加工の全画素を既知の値と比較する
	bool ExpectShaderGraphPreview(ShaderGraphNodePreviewGPUFixture& fixture, const ShaderGraphNodePreviewCase& testCase,
		const Engine::Vector4& expected, uint32_t slot = 0, float time = 1.25f);
	// 原点の値を通すSubGraphを取得する
	Engine::ShaderGraphAsset CreateShaderGraphPreviewSubGraph();
	// 独立評価と保存項目を検証する
	bool TestShaderGraphNodePreviewContracts();
	// 内部設定と外部Assetの変更を更新判定へ含める
	bool TestShaderGraphNodePreviewCache();
	// 既定値と各出力の評価Shaderを全登録Nodeで検証する
	bool TestShaderGraphNodePreviewCatalog(ShaderGraphNodePreviewGPUFixture& fixture);
	// 可変型入力を全ての数値幅で接続してDXCで検証する
	bool TestShaderGraphNodePreviewTypes();
	// 符号と型とAlphaの計算をGPUで確認する
	bool TestShaderGraphNodePreviewValues(ShaderGraphNodePreviewGPUFixture& fixture);
	// 追加した汎用演算の既知値をGPUで確認する
	bool TestShaderGraphGenericNodePreviewValues(ShaderGraphNodePreviewGPUFixture& fixture);
	// SamplerとTextureの出力をGPUで確認する
	bool TestShaderGraphNodePreviewTextures(ShaderGraphNodePreviewGPUFixture& fixture);
	// 時刻と関数とKeywordとSubGraphをGPUで確認する
	bool TestShaderGraphNodePreviewDependencies(ShaderGraphNodePreviewGPUFixture& fixture);
	// ノード評価をCPUと実GPUで確認する
	bool TestShaderGraphNodePreviews();
	// 操作確認用ProjectをGeneratedへ作成する
	bool WriteShaderGraphNodePreviewFixtures();
	// 表示用の全GPU出力を画像へ保存する
	bool WriteShaderGraphNodePreviewImages();
}
