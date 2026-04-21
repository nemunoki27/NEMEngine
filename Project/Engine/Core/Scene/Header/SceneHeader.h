#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetTypes.h>
#include <Engine/MathLib/Color.h>

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

		RGBA8_UNORM,
		RGBA16_FLOAT,
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
		std::optional<Color> clearColor = std::nullopt;
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

		// 描画フォーマット
		SceneRenderTargetFormat colorFormat = SceneRenderTargetFormat::RGBA32_FLOAT;

		// UAVを作成するかどうか
		bool createUAV = false;
		// 深度バッファを作成するかどうか
		bool withDepth = false;
	};

	// シーンのパスの種類
	enum class ScenePassType :
		uint8_t {

		Clear,
		DepthPrepass,
		Draw,
		PostProcess,
		Compute,
		RenderScene,
		Blit,
		Raytracing,
	};

	// 深度プリパスの情報
	struct DepthPrepassPassDesc {

		std::string queue = "Opaque";
		std::string passName = "ZPrepass";
		RenderTargetSetReference dest;
	};

	// 描画パスの情報
	struct DrawPassDesc {

		// 描画名
		std::string queue;
		std::string passName = "Draw";
		// 描画に使用するレンダーターゲット情報
		RenderTargetSetReference dest;
	};

	// ポストエフェクトパスの情報
	struct PostProcessPassDesc {

		// ポストエフェクトマテリアル
		AssetID material{};
		// 入力と出力のレンダーターゲット情報
		RenderTargetSetReference source;
		RenderTargetSetReference dest;
	};

	// コンピュートパスの種類
	enum class ComputeDispatchMode :
		uint8_t {
		
		Fixed,
		FromSourceSize,
		FromDestSize,
	};

	// コンピュートパスの情報
	struct ComputePassDesc {

		// コンピュートシェーダーマテリアル
		AssetID material{};
		std::string passName = "Compute";

		RenderTargetSetReference source;
		RenderTargetSetReference dest;

		ComputeDispatchMode dispatchMode = ComputeDispatchMode::FromDestSize;
		uint32_t groupCountX = 1;
		uint32_t groupCountY = 1;
		uint32_t groupCountZ = 1;
	};

	// クリアパスの情報
	struct ClearPassDesc {

		// 描画に使用するレンダーターゲット情報
		RenderTargetSetReference dest;

		// 色情報をクリアするか
		bool clearColor = true;
		std::optional<Color> clearColorValue = std::nullopt;
		// 深度情報をクリアするか
		bool clearDepth = false;
		float clearDepthValue = 1.0f;
		// ステンシルをクリアするか
		bool clearStencil = false;
		uint8_t clearStencilValue = 0;
	};

	// シーンの描画パスの情報
	struct RenderScenePassDesc {

		// シーンが持つサブシーンのスロット名
		std::string subSceneSlot;
		// 描画に使用するレンダーターゲット情報
		RenderTargetSetReference dest;
	};

	// ブリットパスの情報
	struct BlitPassDesc {

		// 使用されるマテリアルID
		AssetID material{};
		RenderTargetSetReference source;
		RenderTargetSetReference dest;
	};

	// レイトレーシングパスの情報
	struct RaytracingPassDesc {

		AssetID material{};
		std::string passName = "Raytracing";
		RenderTargetSetReference source;
		RenderTargetSetReference dest;
	};

	// サブシーンのスロットの情報
	struct SubSceneSlotDesc {

		// 名前
		std::string slotName;
		// シーンアセットID
		AssetID sceneAsset{};

		// シーンが有効かどうか
		bool enabled = true;
	};

	// シーンのパスの情報
	struct ScenePassDesc {

		// 処理を行うパスの種類
		ScenePassType type = ScenePassType::Draw;

		// タイプに応じて使用する
		ClearPassDesc clear;
		DepthPrepassPassDesc depthPrepass;
		DrawPassDesc draw;
		PostProcessPassDesc postProcess;
		ComputePassDesc compute;
		RenderScenePassDesc renderScene;
		BlitPassDesc blit;
		RaytracingPassDesc raytracing;
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

		// シーンの描画、パスの情報
		std::vector<SceneRenderTargetDesc> renderTargets;
		std::vector<ScenePassDesc> passOrder;
	};

	// json変換
	bool FromJson(const nlohmann::json& data, SceneHeader& sceneHeader, AssetDatabase* assetDatabase);
	nlohmann::json ToJson(const SceneHeader& sceneHeader);
} // Engine
