#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/RenderPipelineAsset.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
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
	// マテリアルを使用する描画機能
	enum class MaterialUsage :
		uint8_t {

		Generic,
		Mesh,
		Particle,
		Sprite,
		Text,
		Line,
		FillFaceMesh,
	};

	// マテリアル内の固定パス種別
	enum class MaterialPassKind :
		uint8_t {

		Invalid = 0,
		ZPrepass,
		EditorPicking,
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
		// パイプラインのステージを上書きする部分シェーダー
		AssetID shaderOverride{};
		// パイプラインバリアントの種類
		PipelineVariantKind preferredVariant = PipelineVariantKind::GraphicsVertex;
	};

	// マテリアルアセットの情報
	struct MaterialAsset {

		// アセットID
		AssetID guid{};
		// マテリアルの名前
		std::string name;
		// マテリアルの種類
		MaterialDomain domain = MaterialDomain::Surface;
		// マテリアルを使用する描画機能
		MaterialUsage usage = MaterialUsage::Generic;

		// 使用されるパスのリスト
		std::vector<MaterialPassBinding> passes;
		// パラメーターの名前と値のマップ
		std::unordered_map<std::string, MaterialParameterValue> parameters;
	};

	// json変換
	bool FromJson(const nlohmann::json& data, MaterialAsset& outAsset);
	nlohmann::json ToJson(const MaterialAsset& asset);

	// マテリアルアセットからパス情報を検索する
	const MaterialPassBinding* FindPass(const MaterialAsset& asset, MaterialPassKind passKind);
} // Engine
