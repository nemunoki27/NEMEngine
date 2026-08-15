#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <string>
#include <vector>
#include <optional>
// json
#include <json.hpp>

namespace Engine {

	// front
	class AssetDatabase;

	//============================================================================
	//	SceneHeader structures
	//============================================================================
	// レンダーターゲットの参照情報
	struct RenderTargetSetReference {

		// MRTのカラー出力名
		std::vector<std::string> colors;
		// 深度
		std::optional<std::string> depth;
	};

	// レンダーターゲットのフォーマット
	enum class SceneRenderTargetFormat :
		uint8_t {

		R8_UNORM,
		R16_FLOAT,
		RG16_FLOAT,
		RGBA8_UNORM,
		RGBA16_FLOAT,
		R32_FLOAT,
		RG32_FLOAT,
		RGBA32_FLOAT,
	};

	// レンダーターゲットのサイズのモード
	enum class SceneRenderTargetSizeMode :
		uint8_t {

		ViewRelative,
		Fixed,
	};

	// シーンのレンダーターゲットのカラー出力の情報
	struct SceneRenderTargetColorDesc {

		// アタッチメント名
		std::string name;

		// フォーマット
		SceneRenderTargetFormat format = SceneRenderTargetFormat::RGBA32_FLOAT;

		// このカラー添付の既定クリア色
		std::optional<Color4> clearColor = std::nullopt;
		// UAVを作るか
		bool createUAV = false;
	};

	// シーンのレンダーターゲット情報
	struct SceneRenderTargetDesc {

		// 名前
		std::string name;
		// サイズのモード
		SceneRenderTargetSizeMode sizeMode = SceneRenderTargetSizeMode::ViewRelative;

		// サイズのスケール
		float widthScale = 1.0f;
		float heightScale = 1.0f;
		// 修正後のサイズ
		uint32_t fixedWidth = 0;
		uint32_t fixedHeight = 0;

		// カラー出力の情報
		std::vector<SceneRenderTargetColorDesc> colors;

		// 深度バッファを作成するかどうか
		bool withDepth = false;
	};

	// サブシーンのスロットの情報
	struct SubSceneSlotDesc {

		// スロットの安定ID
		UUID slotID{};
		// 名前
		std::string slotName;
		// シーンアセットID
		AssetID sceneAsset{};

		// シーンが有効かどうか
		bool enabled = true;
	};

	// 描画レイヤーの情報
	struct RenderLayerDesc {

		// レイヤーID
		int32_t id = 0;
		// レイヤー名
		std::string name;
	};

	// シーンのヘッダー情報
	struct SceneHeader {

		// シーン描画情報のGUIDと名前
		AssetID guid{};
		std::string name;

		// シーンが持つサブシーンリスト
		std::vector<SubSceneSlotDesc> subScenes;

		// シーンのカラー出力、Compute、DispatchRaysをまとめたProfile
		AssetID renderFeatureProfile{};
	};

	// json変換
	std::string MakeDefaultRenderFeatureProfilePath(
		const std::string& scenePath);
	void EnsureSceneRenderFeatureProfile(SceneHeader& sceneHeader,
		const std::string& scenePath, AssetDatabase* assetDatabase);
	bool FromJson(const nlohmann::json& data, SceneHeader& sceneHeader, AssetDatabase* assetDatabase);
	nlohmann::json ToJson(const SceneHeader& sceneHeader);
} // Engine
