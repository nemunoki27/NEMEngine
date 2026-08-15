#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/RenderPipelineAsset.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <string_view>
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
		RayTracing,
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
		RayTracing,
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

	// マテリアルがRendererの描画順序とブレンドを決定する場合の設定
	struct MaterialRenderState {

		bool overridesRenderer = false;
		RenderPhase phase = RenderPhase::Opaque;
		BlendMode blendMode = BlendMode::Normal;
		bool castShadows = true;
		bool receiveShadows = true;
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
		// 見た目を生成するShader Graph
		AssetID shaderGraph{};
		// Shader Graphなど見た目と描画状態を一体で扱うMaterialの設定
		MaterialRenderState renderState{};

		// 使用されるパスのリスト
		std::vector<MaterialPassBinding> passes;
		// ID順に保持するマテリアル既定値
		MaterialParameterSet parameters;
	};

	// json変換
	bool FromJson(const nlohmann::json& data, MaterialAsset& outAsset);
	nlohmann::json ToJson(const MaterialAsset& asset);

	// 標準PBR描画に必要なPassと既定値を持つMeshマテリアルを生成する
	MaterialAsset CreateDefaultMeshMaterialAsset(std::string_view name);

	// マテリアルアセットからパス情報を検索する
	MaterialPassBinding* FindPass(MaterialAsset& asset, MaterialPassKind passKind);
	const MaterialPassBinding* FindPass(const MaterialAsset& asset, MaterialPassKind passKind);
} // Engine
