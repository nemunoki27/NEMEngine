#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Foundation/Math/Color.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

// c++
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	ColorPipeline structures
	//============================================================================
	// シーン露出の決定方法
	enum class ExposureMode : uint8_t {

		Manual,
		Automatic,
	};

	// 露出と適応範囲の設定
	struct ExposureSettings {

		ExposureMode mode = ExposureMode::Manual;
		float manualEV100 = 0.0f;
		float compensation = 0.0f;
		float minEV100 = -10.0f;
		float maxEV100 = 20.0f;
		float histogramLowPercent = 0.8f;
		float histogramHighPercent = 0.95f;
		float speedUp = 3.0f;
		float speedDown = 1.0f;
		bool usePreExposure = true;
	};

	// ACES変換後のフィルム特性
	struct FilmicToneMapSettings {

		float slope = 1.0f;
		float toe = 0.0f;
		float shoulder = 0.0f;
		float blackClip = 0.0f;
		float whiteClip = 0.0f;
	};

	// ToneMap前のシーン共通カラー補正
	struct ColorGradingSettings {

		Color4 colorFilter = Color4::White();
		float temperature = 6500.0f;
		float tint = 0.0f;
		Vector3 saturation = Vector3::AnyInit(1.0f);
		Vector3 contrast = Vector3::AnyInit(1.0f);
		Vector3 gamma = Vector3::AnyInit(1.0f);
		Vector3 gain = Vector3::AnyInit(1.0f);
		Vector3 offset = Vector3::AnyInit(0.0f);
	};

	// HDRカラーから表示色までの共通設定
	struct ColorPipelineSettings {

		ExposureSettings exposure{};
		FilmicToneMapSettings filmic{};
		ColorGradingSettings colorGrading{};
	};

	//============================================================================
	//	RenderFeatureProfile structures
	//============================================================================
	// GPUへ発行するPassの種類
	enum class RenderFeaturePassType : uint8_t {

		Compute,
		RayTracing,
	};

	// 固定RenderPathへFeatureを差し込む位置
	enum class RenderFeatureAnchor : uint8_t {

		BeforeLighting,
		AfterLighting,
		BeforeTransparent,
		AfterTransparent,
		AfterMaskedUI,
		BeforeBlit,
	};

	// Feature出力テクスチャの形式
	enum class RenderFeatureTextureFormat : uint8_t {

		Inherit,
		R8_UNORM,
		R16_FLOAT,
		RG16_FLOAT,
		RGBA16_FLOAT,
		R32_FLOAT,
		RG32_FLOAT,
		RGBA32_FLOAT,
	};

	// Passの主入力を解決する方法
	enum class RenderFeatureSourceKind : uint8_t {

		PreviousPass,
		SceneColor,
		PassOutput,
	};

	// 別Passの名前付き出力への参照
	struct RenderFeatureOutputReference {

		UUID pass{};
		std::string output = "Color";
	};

	// Passが生成する名前付きUAV
	struct RenderFeatureOutputSettings {

		std::string name = "Color";
		std::string shaderResource = "gDestColor";
		RenderFeatureTextureFormat format =
			RenderFeatureTextureFormat::Inherit;
		float widthScale = 1.0f;
		float heightScale = 1.0f;
		// 前フレームの同一出力をSRVとして参照する場合にping-pongする
		bool history = false;
		std::string historyShaderResource{};
		std::optional<Color4> clearColor{};
	};

	// Profile内の1つのGPU Pass
	struct RenderFeaturePassSettings {

		UUID id{};
		std::string name = "Render Feature";
		bool enabled = true;
		bool gameView = true;
		bool sceneView = true;
		RenderFeaturePassType type = RenderFeaturePassType::Compute;
		RenderFeatureAnchor anchor = RenderFeatureAnchor::AfterTransparent;
		AssetID material{};
		MaterialPassKind materialPass = MaterialPassKind::PostProcess;
		RenderFeatureSourceKind sourceKind =
			RenderFeatureSourceKind::PreviousPass;
		RenderFeatureOutputReference source{};
		bool sceneColorOutput = false;
		uint32_t targetMask = 0u;
		uint32_t rayGenerationIndex = 0u;
		// GPU時間を予算内へ収めるため出力解像度を段階的に調整する
		bool adaptiveResolution = false;
		float gpuBudgetMs = 2.0f;
		float minResolutionScale = 0.5f;
		float maxResolutionScale = 1.0f;
		float resolutionStep = 0.25f;
		uint32_t adjustmentIntervalFrames = 30u;
		std::vector<RenderFeatureOutputSettings> outputs{};
		MaterialParameterSet parameterOverrides{};
		std::unordered_map<std::string, AssetID> textureOverrides{};
		std::unordered_map<std::string, std::string> sceneInputs{};
		std::unordered_map<std::string, RenderFeatureOutputReference>
			passInputs{};
		std::unordered_map<std::string, PipelineStaticSamplerSettings>
			samplerOverrides{};
	};

	// シーンが使用する描画Feature全体
	struct RenderFeatureProfileAsset {

		AssetID guid{};
		std::string name = "Render Feature Profile";
		uint32_t version = 1u;
		ColorPipelineSettings colorPipeline{};
		std::vector<RenderFeaturePassSettings> passes{};
	};

	// Anchorの固定RenderPath上の順序を取得する
	uint32_t GetRenderFeatureAnchorOrder(RenderFeatureAnchor anchor);
} // Engine
