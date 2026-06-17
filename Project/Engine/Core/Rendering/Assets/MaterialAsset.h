#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/RenderPipelineAsset.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Math.h>

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

	// マテリアル内の固定パス種別
	enum class MaterialPassKind :
		uint8_t {

		Invalid = 0,
		ZPrepass,
		Draw,
		Transparent,
		Outline,
		OutlineStencilWrite,
		OutlineStencilTest,
		ScreenSpaceOutlineMask,
		ScreenSpaceOutlineCoverageMask,
		ScreenSpaceOutlineDilateHorizontal,
		ScreenSpaceOutlineDilateVertical,
		ScreenSpaceOutlineComposite,
		Blit,
		Fullscreen,
		PostProcess,
		Reflection,
	};

	// マテリアルのパス情報
	struct MaterialPassBinding {

		// パスの種類
		MaterialPassKind passKind = MaterialPassKind::Invalid;
		// 使用されるパイプラインアセット
		AssetID pipeline{};
		// パイプラインバリアントの種類
		PipelineVariantKind preferredVariant = PipelineVariantKind::GraphicsVertex;
	};

	// マテリアルのパラメーター値
	struct MaterialParameterValue {

		std::variant<float, Vector2, Vector3, Vector4, Color4, AssetID, int32_t, uint32_t, bool> value;
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

	// 単一パラメータ値のjson変換でサブメッシュ側のparameterOverridesでも共用する
	bool ParseMaterialParameterValue(const nlohmann::json& data, MaterialParameterValue& outValue);
	nlohmann::json SerializeMaterialParameterValue(const MaterialParameterValue& parameter);

	// マテリアルアセットからパス情報を検索する
	const MaterialPassBinding* FindPass(const MaterialAsset& asset, MaterialPassKind passKind);
} // Engine
