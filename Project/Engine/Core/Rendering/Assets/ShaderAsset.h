#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <span>

namespace Engine {

	//============================================================================
	//	ShaderAsset structures
	//============================================================================
	// シェーダーステージの情報
	struct ShaderStageEntry {

		// シェーダーの種類
		ShaderStage stage{};

		// シェーダーファイルのパス
		std::string file;
		std::string entry = "main";
		std::string profile;
		// 合成後もCook済みステージを元Shader Assetから引くための実行時ID
		AssetID ownerShader{};
	};
	// HLSL変数名とMaterial Instanceの安定IDを対応付けるメタデータ
	struct ShaderParameterMetadata {

		std::string shaderName;
		std::string displayName;
		MaterialParameterID id{};
		MaterialParameterSemantic semantic =
			MaterialParameterSemantic::None;
		bool isColor = false;
		bool isTexture = false;
	};
	// シェーダーアセットの情報
	struct ShaderAsset {

		// アセットID
		AssetID guid{};
		// シェーダーの名前
		std::string name;

		// 使用されるシェーダーリスト
		std::vector<ShaderStageEntry> stages;
		// 色として編集するマテリアルパラメータ名、reflectionに色情報が無いのでここで宣言する
		std::vector<std::string> colorParameters;
		// Shader Graph等が生成した安定IDと表示情報
		std::vector<ShaderParameterMetadata> parameters;
	};

	// json変換
	bool FromJson(const nlohmann::json& data, ShaderAsset& outAsset);
	nlohmann::json ToJson(const ShaderAsset& asset);

	// シェーダーステージを検索する
	const ShaderStageEntry* FindShaderStage(const ShaderAsset& asset, ShaderStage stage);
	// シェーダーエクスポートを検索する
	const ShaderStageEntry* FindShaderExport(const ShaderAsset& asset,
		ShaderStage stage, std::string_view entry);
	// 部分シェーダーのエクスポートを同じ識別子へ上書きする
	void OverlayShaderExports(ShaderAsset& target, const ShaderAsset& source);
	// Shader Assetのメタデータをコンパイル済みReflectionへ反映する
	void ApplyShaderParameterMetadata(
		ShaderReflectionInfo& reflection,
		const ShaderAsset& asset);
} // Engine
