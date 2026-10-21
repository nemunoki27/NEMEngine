#include "EditorRenderBenchmark.h"
#include "EngineApplication.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Views/ViewportRenderService.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetNames.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/CameraSurfacePipeline.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/RenderFeatureResourceUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <Externals/DirectXTex/DirectXTex.h>

void Engine::EditorRenderBenchmark::InitCaptureSettings() {

	const auto mode = Algorithm::GetEnvironmentPath(L"NEM_RENDER_BENCHMARK_CAPTURE").native();
	if (mode.empty() || mode == L"Final") captureMode_ = CaptureMode::Final;
	else if (mode == L"None") captureMode_ = CaptureMode::None;
	else if (mode == L"Intermediate") captureMode_ = CaptureMode::Intermediate;
	else if (mode == L"HDR") captureMode_ = CaptureMode::HDR;
	else throw std::runtime_error("描画画像の保存範囲が不正です");
	report_["captureMode"] = mode.empty() ? "Final" : Algorithm::PathToUTF8(std::filesystem::path(mode));
}

void Engine::EditorRenderBenchmark::CaptureViews(GraphicsCore& graphicsCore) {

	if (captureMode_ == CaptureMode::None) return;
	// 画像の読戻し待機は計測区間の外へ置く
	for (const auto kind : { RenderViewKind::Game, RenderViewKind::Scene }) {
		const std::string aoRaw = RenderFeatureResourceUtility::MakeOutputAlias({CameraSurfacePassIDs::AmbientOcclusionTrace, "Raw"});
		const std::string aoFiltered = RenderFeatureResourceUtility::MakeOutputAlias({CameraSurfacePassIDs::AmbientOcclusionFilter, "Color"});
		for (const char* attachment : { "", "InlineShadowRaw", "InlineShadowFiltered", aoRaw.c_str(), aoFiltered.c_str(), RenderTargetNames::kSceneColorMain, RenderTargetNames::kSceneNormalMain,
			RenderTargetNames::kScenePositionMain, RenderTargetNames::kSceneMaterialMain,
			RenderTargetNames::kSceneColorFinal, ViewportRenderService::GetPrimaryColorName(kind) }) {
			// 通常の確認では最終出力だけを保存する
			if (captureMode_ == CaptureMode::Final && *attachment) continue;
			const auto* texture = application_->GetRenderedViewTexture(kind, attachment);
			if (!texture || !texture->IsValid()) {
				continue;
			}
			DirectX::ScratchImage captured;
			const auto state = texture->GetCurrentState();
			if (FAILED(DirectX::CaptureTexture(graphicsCore.GetDXObject().GetCommandQueue()->GetQueue(),
				texture->GetResource(), false, captured, state, state))) {
				throw std::runtime_error("Viewの画像取得に失敗しました");
			}
			DirectX::ScratchImage converted;
			const DirectX::Image* image = captured.GetImage(0, 0, 0);
			if (attachment == aoRaw || attachment == aoFiltered) {

				// AOを浮動小数点で読み戻し、遮蔽率を記録する
				DirectX::ScratchImage values;
				if (FAILED(DirectX::Convert(*image, DXGI_FORMAT_R32_FLOAT,
					DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, values))) {
					throw std::runtime_error("AOの数値取得に失敗しました");
				}
				const auto* data = values.GetImage(0, 0, 0);
				double sum = 0.0;
				float minimum = 1.0f;
				size_t occluded = 0;
				for (size_t y = 0; y < data->height; ++y) {
					for (size_t x = 0; x < data->width; ++x) {
						float value;
						std::memcpy(&value, data->pixels + y * data->rowPitch + x * sizeof(float), sizeof(float));
						if (!std::isfinite(value) || value < 0.0f || value > 1.0f) throw std::runtime_error("AOの値が範囲外です");
						sum += value;
						minimum = std::min(minimum, value);
						if (value < 0.99f) ++occluded;
					}
				}
				const double count = static_cast<double>(data->width * data->height);
				report_["ambientOcclusionCaptures"].push_back({{"phase", phase_}, {"view", kind == RenderViewKind::Game ? "Game" : "Scene"},
					{"output", attachment == aoRaw ? "Raw" : "Filtered"}, {"minimum", minimum}, {"mean", sum / count}, {"occludedRatio", occluded / count}});
			}
			// 表示用の色だけをPNGの色空間へ合わせる
			const bool displayColor = !*attachment || std::string_view(attachment) == RenderTargetNames::kSceneColorFinal ||
				std::string_view(attachment) == ViewportRenderService::GetPrimaryColorName(kind);
			const auto format = displayColor ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
			if (image->format != format) {
				if (FAILED(DirectX::Convert(*image, format,
					DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted))) {
					throw std::runtime_error("Viewの画像変換に失敗しました");
				}
				image = converted.GetImage(0, 0, 0);
			}
			const std::string suffix = *attachment ? std::string("-") + attachment : "";
			const auto filename = std::to_string(phase_) + (kind == RenderViewKind::Game ? "-Game" : "-Scene") + suffix + ".png";
			const auto path = output_.parent_path() / filename;
			if (FAILED(DirectX::SaveToWICFile(*image, DirectX::WIC_FLAGS_NONE,
				DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), path.c_str()))) {
				throw std::runtime_error("Viewの画像保存に失敗しました");
			}
			if (captureMode_ == CaptureMode::HDR) {
				// 照明結果とGBufferを丸めずに比較用へ残す
				auto dataPath = path;
				dataPath.replace_extension(".dds");
				if (FAILED(DirectX::SaveToDDSFile(captured.GetImages(), captured.GetImageCount(), captured.GetMetadata(),
					DirectX::DDS_FLAGS_NONE, dataPath.c_str()))) {
					throw std::runtime_error("Viewの数値保存に失敗しました");
				}
			}
		}
	}
}

