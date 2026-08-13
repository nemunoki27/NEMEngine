#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessAnchor.h>
#include <Engine/Core/Foundation/Math/Color.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

// c++
#include <string>
#include <vector>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	ColorPipeline structures
	//============================================================================
	// シーンの露出を固定値と輝度ヒストグラムのどちらで決めるか
	enum class ExposureMode :
		uint8_t {

		Manual,
		Automatic,
	};

	// EV100と自動露出の適応範囲を保持する
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

	// ACES fitted後のフィルム特性を調整する
	struct FilmicToneMapSettings {

		float slope = 1.0f;
		float toe = 0.0f;
		float shoulder = 0.0f;
		float blackClip = 0.0f;
		float whiteClip = 0.0f;
	};

	// ToneMap前に適用するシーン共通カラー補正
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

	// シーンのHDRカラーから表示用カラーまでの共通設定
	struct ColorPipelineSettings {

		ExposureSettings exposure{};
		FilmicToneMapSettings filmic{};
		ColorGradingSettings colorGrading{};
	};

	//============================================================================
	//	PostProcessStack structures
	//============================================================================
	// ポストプロセスの1パス分の設定データ
	struct PostProcessStackPassSettings {

		// パスの識別IDで並び替え後も参照を保つ
		UUID id{};
		// 表示名
		std::string name;
		// 有効フラグ
		bool enabled = true;
		// 実行するMaterialアセットのGUID
		AssetID materialGuid{};
		// 実行するパス種別
		MaterialPassKind passKind = MaterialPassKind::PostProcess;
		// このパスを差し込む固定パス上の位置
		PostProcessAnchor anchor = PostProcessAnchor::AfterMaskedUI;
		// 主入力に使うパス、空なら同じAnchor内の直前パスを使う
		UUID sourcePass{};
		// 同じAnchor内の最終出力としてSceneFinalへ戻す
		bool graphOutput = false;
		// 0なら全画面、1以上ならGBufferへ描画した不透明3Dの対象マスクと一致する画素だけ適用する
		uint32_t targetMask = 0u;
		// CBufferパラメータのScene毎overrideマップ
		MaterialParameterSet parameterOverrides;
		// TextureのScene毎overrideマップ
		std::unordered_map<std::string, AssetID> textureGuids;
		// SRVバインド名から中間RT名(GBuffer/深度など)への割り当て、.pngより優先される
		std::unordered_map<std::string, std::string> renderTargetInputs;
		// SRVバインド名から任意の先行パス出力への割り当て
		std::unordered_map<std::string, UUID> passInputs;
		// SamplerState名から静的サンプラー設定へのScene毎overrideマップ
		std::unordered_map<std::string, PipelineStaticSamplerSettings> samplerOverrides;
	};

	// シーンごとのPostProcessStackの設定データ
	struct PostProcessStackSettings {

		int version = 3;
		ColorPipelineSettings colorPipeline{};
		std::vector<PostProcessStackPassSettings> passes;
	};
} // Engine
