#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Asset/RenderPipelineAsset.h>
#include <Engine/Core/Asset/AssetTypes.h>
#include <Engine/MathLib/Math.h>

// c++
#include <variant>

namespace Engine {

	//============================================================================
	//	MaterialAsset structures
	//============================================================================

	// マテリアルの種類
	enum class MaterialDomain :
		uint8_t {

		Surface,
		UI,
		Fullscreen,
		Compute,
	};

	// マテリアルのパス情報
	struct MaterialPassBinding {

		// パスの名前
		std::string passName;
		// 使用されるパイプラインアセット
		AssetID pipeline{};
		// パイプラインバリアントの種類
		PipelineVariantKind preferredVariant = PipelineVariantKind::GraphicsVertex;
	};

	// マテリアルのパラメーター値
	struct MaterialParameterValue {

		std::variant<float, Vector2, Vector3, Vector4, Color4, AssetID, int32_t, uint32_t> value;
	};
	
	// マテリアルアセットの情報
	struct MaterialAsset {

		// アセットID
		AssetID guid{};
		// マテリアルの名前
		std::string name;
		// マテリアルの種類
		MaterialDomain domain = MaterialDomain::Surface;

		// 使用されるパスのリスト
		std::vector<MaterialPassBinding> passes;
		// パラメーターの名前と値のマップ
		std::unordered_map<std::string, MaterialParameterValue> parameters;
	};

	// json変換
	bool FromJson(const nlohmann::json& data, MaterialAsset& outAsset);
	nlohmann::json ToJson(const MaterialAsset& asset);

	// マテリアルアセットからパス情報を検索する
	const MaterialPassBinding* FindPass(const MaterialAsset& asset, const std::string_view& passName);
} // Engine