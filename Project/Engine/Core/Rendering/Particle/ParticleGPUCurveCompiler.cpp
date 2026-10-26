#include "ParticleGPUCurveCompiler.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

using namespace Engine;

namespace {

	// 各成分の絶対誤差と相対誤差を確認する
	bool WithinError(const Vector4& actual, const Vector4& approximation) {

		const float* a = &actual.x;
		const float* b = &approximation.x;
		for (uint32_t i = 0; i < 4; ++i) {
			if (!std::isfinite(a[i]) || !std::isfinite(b[i]) || std::abs(a[i] - b[i]) > 0.001f * (std::max)(1.0f, std::abs(a[i]))) return false;
		}
		return true;
	}

	// 入れ子の曲線から境界時刻と繰り返し数を集める
	void CollectCurveKeys(const nlohmann::json& value, std::vector<float>& keys, std::vector<uint32_t>& loops) {

		if (value.is_object()) {
			if (value.contains("time") && value["time"].is_number()) keys.push_back(value["time"].get<float>());
			if (value.contains("loopCount")) {
				const int32_t count = value["loopCount"].get<int32_t>();
				if (count > 4096) throw std::invalid_argument("GPUの曲線ループ上限は4096です");
				if (count > 1) loops.push_back(static_cast<uint32_t>(count));
			}
		}
		if (value.is_structured()) for (const auto& child : value) CollectCurveKeys(child, keys, loops);
	}

	// 狭い変化とループ境界も誤差の確認対象にする
	std::vector<float> MakeCurveCheckPoints(const nlohmann::json& parameters) {

		std::vector<float> keys{0.0f, 1.0f};
		std::vector<uint32_t> loops{1};
		CollectCurveKeys(parameters, keys, loops);
		std::sort(loops.begin(), loops.end());
		loops.erase(std::unique(loops.begin(), loops.end()), loops.end());
		std::vector<float> result;
		// 狭い山とループ境界も検査し、等間隔の検査から漏らさない
		for (const auto count : loops) for (uint32_t cycle = 0; cycle < count; ++cycle) for (float key : keys) for (bool reverse : {false, true}) {
			if (!std::isfinite(key) || key < 0 || key > 1) continue;
			const float progress = (cycle + (reverse ? 1.0f - key : key)) / count;
			result.push_back(progress);
			result.push_back((std::max)(0.0f, std::nextafter(progress, 0.0f)));
			result.push_back((std::min)(1.0f, std::nextafter(progress, 1.0f)));
		}
		std::sort(result.begin(), result.end());
		result.erase(std::unique(result.begin(), result.end()), result.end());
		return result;
	}

	// 区間内の曲がりが許容誤差へ収まるか確認する
	bool CheckInterval(const ParticleGPUCurveEvaluator& evaluate, const ParticleGPUCurveSample& a,
		const ParticleGPUCurveSample& b) {

		const float duration = b.timing.x - a.timing.x;
		if (duration <= 0) return true;
		for (const float ratio : {0.25f, 0.5f, 0.75f}) {
			const float time = std::lerp(a.timing.x, b.timing.x, ratio);
			if (time <= a.timing.x || time >= b.timing.x) continue;
			const float blend = (time - a.timing.x) / duration;
			const auto actual = evaluate(time);
			if (!WithinError(actual.value0, a.value0 + (b.value0 - a.value0) * blend) ||
				!WithinError(actual.value1, a.value1 + (b.value1 - a.value1) * blend)) return false;
		}
		return true;
	}

	// 等間隔では表せない折り返しと段差だけ標本位置を分ける
	std::vector<ParticleGPUCurveSample> CompileAdaptive(const ParticleGPUCurveEvaluator& evaluate,
		std::vector<float> times) {

		for (uint32_t i = 0; i <= 256; ++i) times.push_back(float(i) / 256);
		std::sort(times.begin(), times.end());
		times.erase(std::unique(times.begin(), times.end()), times.end());
		if (times.size() > 4097) throw std::invalid_argument("GPUの曲線精度上限を超えています");
		std::vector<ParticleGPUCurveSample> samples;
		for (const float time : times) {
			auto sample = evaluate(time);
			if (!WithinError(sample.value0, sample.value0) || !WithinError(sample.value1, sample.value1))
				throw std::invalid_argument("GPUの曲線には有限の値を指定してください");
			sample.timing = Vector4(time, 1, 0, 0);
			samples.push_back(sample);
		}
		for (;;) {
			std::vector<ParticleGPUCurveSample> refined;
			refined.reserve(samples.size() * 2);
			for (size_t i = 0; i + 1 < samples.size(); ++i) {
				refined.push_back(samples[i]);
				if (CheckInterval(evaluate, samples[i], samples[i + 1])) continue;
				const float time = std::midpoint(samples[i].timing.x, samples[i + 1].timing.x);
				if (time <= samples[i].timing.x || time >= samples[i + 1].timing.x)
					throw std::invalid_argument("GPUの曲線時刻を分割できません");
				auto middle = evaluate(time);
				middle.timing = Vector4(time, 1, 0, 0);
				refined.push_back(middle);
			}
			refined.push_back(samples.back());
			if (refined.size() == samples.size()) return samples;
			if (refined.size() > 4097) throw std::invalid_argument("GPUの曲線精度上限を超えています");
			samples.swap(refined);
		}
	}

}

void Engine::CompileParticleGPUCurve(const nlohmann::json& parameters, const ParticleGPUCurveEvaluator& evaluate,
	ParticleGPUInstruction& instruction, ParticleGPUProgram& program) {

	// CPUの評価器を使い、許容誤差を満たすまで共有テーブルを細かくする
	std::vector<ParticleGPUCurveSample> samples;
	const auto checkPoints = MakeCurveCheckPoints(parameters);
	for (uint32_t count = 257; count <= 4097; count = (count - 1) * 2 + 1) {
		samples.resize(count);
		for (uint32_t i = 0; i < count; ++i) {
			samples[i] = evaluate(float(i) / (count - 1));
			samples[i].timing = Vector4(float(i) / (count - 1), 0, 0, 0);
		}
		bool valid = true;
		for (const float progress : checkPoints) {
			const float coordinate = progress * (count - 1);
			const uint32_t index = (std::min)(static_cast<uint32_t>(coordinate), count - 2);
			const float t = coordinate - index;
			const auto actual = evaluate(progress);
			valid &= WithinError(actual.value0, samples[index].value0 * (1 - t) + samples[index + 1].value0 * t) &&
				WithinError(actual.value1, samples[index].value1 * (1 - t) + samples[index + 1].value1 * t);
			if (!valid) break;
		}
		for (uint32_t i = 0; valid && i + 1 < count; ++i) {
			for (const float t : {0.25f, 0.5f, 0.75f}) {
				const auto actual = evaluate((float(i) + t) / (count - 1));
				valid &= WithinError(actual.value0, samples[i].value0 * (1 - t) + samples[i + 1].value0 * t) &&
					WithinError(actual.value1, samples[i].value1 * (1 - t) + samples[i + 1].value1 * t);
			}
		}
		if (!valid) continue;
		instruction.curveOffset = static_cast<uint32_t>(program.curves.size());
		instruction.curveCount = count;
		program.curves.insert(program.curves.end(), samples.begin(), samples.end());
		return;
	}
	samples = CompileAdaptive(evaluate, checkPoints);
	instruction.curveOffset = static_cast<uint32_t>(program.curves.size());
	instruction.curveCount = static_cast<uint32_t>(samples.size());
	program.curves.insert(program.curves.end(), samples.begin(), samples.end());
}

