#include "EditorRenderBenchmark.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>

// c++
#include <algorithm>
#include <stdexcept>

void Engine::EditorRenderBenchmark::InitComparisonConditions() {

	InitCaptureSettings();
	// 表示設定の切替を同じEditorのまま比較する
	const auto outputSequence = Algorithm::GetEnvironmentPath(L"NEM_RENDER_BENCHMARK_DISPLAY_OUTPUT");
	if (!outputSequence.empty()) {
		auto sequence = JsonFile::Load(outputSequence, false);
		if (!sequence.is_array() || sequence.empty() || sequence.size() > 128 ||
			std::any_of(sequence.begin(), sequence.end(), [](const auto& value) {
				return !value.is_object() || !value.contains("mode") || !value["mode"].is_number_unsigned() || value["mode"] > 2;
			})) throw std::runtime_error("表示出力の比較指定が不正です");
		phaseCount_ = static_cast<uint32_t>(sequence.size());
		report_["displayOutputSequence"] = std::move(sequence);
		report_["comparison"] = "DisplayOutput";
		report_["phaseCount"] = phaseCount_;
		const auto& original = features_->GetPreferences().displayOutput;
		report_["originalDisplayOutput"] = { {"mode", static_cast<uint32_t>(original.mode)},
			{"paperWhiteNits", original.paperWhiteNits}, {"maxLuminanceNits", original.maxLuminanceNits} };
	}
	// AOの切替とパラメータ変更を同じCameraで比較する
	const auto aoSequence = Algorithm::GetEnvironmentPath(L"NEM_RENDER_BENCHMARK_AO");
	if (!aoSequence.empty()) {

		report_["ambientOcclusionSequence"] = JsonFile::Load(aoSequence, false);
		const auto& sequence = report_["ambientOcclusionSequence"];
		if (!sequence.is_array() || sequence.empty()) throw std::runtime_error("AO比較の指定が不正です");
		phaseCount_ = static_cast<uint32_t>(sequence.size());
		report_["comparison"] = "AmbientOcclusion";
		report_["phaseCount"] = phaseCount_;
	}
	// 影の標本数と切替を隔離したWorldで比較する
	const auto shadowSequence = Algorithm::GetEnvironmentPath(L"NEM_RENDER_BENCHMARK_SHADOW");
	if (!shadowSequence.empty()) {

		report_["shadowSequence"] = JsonFile::Load(shadowSequence, false);
		const auto& sequence = report_["shadowSequence"];
		if (!sequence.is_array() || sequence.empty()) throw std::runtime_error("影比較の指定が不正です");
		phaseCount_ = static_cast<uint32_t>(sequence.size());
		report_["comparison"] = "InlineShadows";
		report_["phaseCount"] = phaseCount_;
	}
	// 配置とSRT変更を同じSceneで再現する
	const auto transformSequence = Algorithm::GetEnvironmentPath(L"NEM_RENDER_BENCHMARK_TRANSFORM");
	if (!transformSequence.empty()) {

		report_["transformSequence"] = JsonFile::Load(transformSequence, false);
		const auto& sequence = report_["transformSequence"];
		if (!sequence.is_array() || sequence.empty()) throw std::runtime_error("SRT比較の指定が不正です");
		phaseCount_ = static_cast<uint32_t>(sequence.size());
		report_["comparison"] = "AssetTransforms";
		report_["phaseCount"] = phaseCount_;
		report_["transformTargets"] = nlohmann::json::object();
	}
	const auto frameSequence = Algorithm::GetEnvironmentPath(L"NEM_RENDER_BENCHMARK_FRAME_CONTEXTS");
	if (!frameSequence.empty()) {
		auto sequence = JsonFile::Load(frameSequence, false);
		if (!sequence.is_array() || sequence.empty() || sequence.size() > 128 ||
			std::any_of(sequence.begin(), sequence.end(), [](const auto& value) {
				return !value.is_number_unsigned() || (value != 1 && value != 2);
			})) throw std::runtime_error("描画フレーム数の比較指定が不正です");
		phaseCount_ = static_cast<uint32_t>(sequence.size());
		report_["phaseCount"] = phaseCount_;
		report_["frameContextSequence"] = std::move(sequence);
	}
}

void Engine::EditorRenderBenchmark::BeginDisplayOutputPhase(GraphicsCore& graphicsCore) {

	if (!report_.contains("displayOutputSequence")) return;
	const auto& settings = report_["displayOutputSequence"][phase_];
	features_->SetDisplayOutputMode(static_cast<DisplayOutputMode>(settings.at("mode").get<uint32_t>()));
	features_->SetDisplayLuminance(settings.value("paperWhiteNits", 200.0f), settings.value("maxLuminanceNits", 1000.0f));
	if (settings.contains("frameContextCount")) graphicsCore.RequestDoubleBuffering(settings.at("frameContextCount") == 2);
}

void Engine::EditorRenderBenchmark::RestoreDisplayOutput() {

	if (!features_ || !report_.contains("originalDisplayOutput")) return;
	const auto& original = report_["originalDisplayOutput"];
	features_->SetDisplayOutputMode(static_cast<DisplayOutputMode>(original.at("mode").get<uint32_t>()));
	features_->SetDisplayLuminance(original.at("paperWhiteNits").get<float>(), original.at("maxLuminanceNits").get<float>());
}
