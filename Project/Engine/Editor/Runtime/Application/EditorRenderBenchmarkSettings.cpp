#include "EditorRenderBenchmark.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>

// c++
#include <stdexcept>

void Engine::EditorRenderBenchmark::InitComparisonConditions() {

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
}
