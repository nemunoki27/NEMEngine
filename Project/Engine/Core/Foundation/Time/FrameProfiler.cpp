#include "FrameProfiler.h"

//============================================================================
//	include
//============================================================================
// c++
#include <numeric>

//============================================================================
//	FrameProfiler classMethods
//============================================================================
Engine::FrameProfiler& Engine::FrameProfiler::GetInstance() {

	static FrameProfiler instance;
	return instance;
}

void Engine::FrameProfiler::BeginFrame(float deltaTimeSec, float totalTimeSec) {

	deltaTimeSec_ = deltaTimeSec;
	totalTimeSec_ = totalTimeSec;

	// 前フレームの累積を履歴へ確定し、今フレームの累積をリセットする
	for (Measure& measure : measures_) {

		if (!firstFrame_) {

			measure.samples.emplace_back(measure.accumulator);
			if (kSmoothingSample < measure.samples.size()) {
				measure.samples.erase(measure.samples.begin());
			}
		}
		measure.accumulator = 0.0f;
	}
	firstFrame_ = false;
	resolvedRenderingStatistics_ = renderingStatistics_;
	renderingStatistics_ = {};
}

void Engine::FrameProfiler::AddSample(Category category, float milliseconds) {

	const size_t index = static_cast<size_t>(category);
	if (index >= measures_.size()) {
		return;
	}
	measures_[index].accumulator += milliseconds;
}

void Engine::FrameProfiler::SetGPUPassTimes(const std::vector<NamedTime>& passes) {

	gpuPassTimes_ = passes;
}

void Engine::FrameProfiler::SetEcsSystemTimes(const std::vector<NamedTime>& systems) {

	ecsSystemTimes_ = systems;
}

void Engine::FrameProfiler::AddMeshUpdate(uint32_t rebuilds, uint32_t transforms, uint32_t parameters,
	uint32_t reused, uint64_t instances) {

	renderingStatistics_.meshRebuildCount += rebuilds;
	renderingStatistics_.meshTransformUpdateCount += transforms;
	renderingStatistics_.meshParameterUpdateCount += parameters;
	renderingStatistics_.meshReuseCount += reused;
	renderingStatistics_.meshUpdatedInstances += instances;
}

void Engine::FrameProfiler::AddSkinningDispatch(uint32_t instanceCount) {

	++renderingStatistics_.skinningDispatchCount;
	renderingStatistics_.skinnedInstanceCount += instanceCount;
}

void Engine::FrameProfiler::AddBLASBuild(uint32_t geometryCount) {

	++renderingStatistics_.blasBuildCount;
	renderingStatistics_.blasGeometryCount += geometryCount;
}

void Engine::FrameProfiler::AddBLASRefit(uint32_t geometryCount) {

	++renderingStatistics_.blasRefitCount;
	renderingStatistics_.blasGeometryCount += geometryCount;
}

void Engine::FrameProfiler::AddBLASSkip(uint32_t geometryCount) {

	++renderingStatistics_.blasSkipCount;
	renderingStatistics_.blasGeometryCount += geometryCount;
}

void Engine::FrameProfiler::SetTLASInstanceCount(uint32_t instanceCount) {

	renderingStatistics_.tlasInstanceCount = instanceCount;
}

void Engine::FrameProfiler::AddTLASBuild() {

	++renderingStatistics_.tlasBuildCount;
}

void Engine::FrameProfiler::AddTLASRefit() {

	++renderingStatistics_.tlasRefitCount;
}

void Engine::FrameProfiler::AddTLASSkip() {

	++renderingStatistics_.tlasSkipCount;
}

void Engine::FrameProfiler::SetClusterStatistics(uint32_t clusterCount,
	uint32_t localLightCount, uint32_t lightIndexCount, uint32_t overflowCount) {

	renderingStatistics_.clusterCount = clusterCount;
	renderingStatistics_.clusterLocalLightCount = localLightCount;
	renderingStatistics_.clusterLightIndexCount = lightIndexCount;
	renderingStatistics_.clusterOverflowCount = overflowCount;
}

void Engine::FrameProfiler::SetFrameContextStatistics(uint32_t contextIndex,
	uint32_t contextCount, uint32_t queuedFrameCount) {

	renderingStatistics_.frameContextIndex = contextIndex;
	renderingStatistics_.frameContextCount = contextCount;
	renderingStatistics_.queuedFrameCount = queuedFrameCount;
}

float Engine::FrameProfiler::GetAverageMs(Category category) const {

	const size_t index = static_cast<size_t>(category);
	if (index >= measures_.size()) {
		return 0.0f;
	}

	const Measure& measure = measures_[index];
	// 確定済み履歴がなければ、計測中の累積値をそのまま返す
	if (measure.samples.empty()) {
		return measure.accumulator;
	}
	const float sum = std::accumulate(measure.samples.begin(), measure.samples.end(), 0.0f);
	return sum / static_cast<float>(measure.samples.size());
}

float Engine::FrameProfiler::GetGPUTotalMs() const {

	float total = 0.0f;
	for (const GPUPassTime& pass : gpuPassTimes_) {
		total += pass.milliseconds;
	}
	return total;
}

float Engine::FrameProfiler::FindGPUPassMs(std::string_view name) const {

	for (const GPUPassTime& pass : gpuPassTimes_) {
		if (pass.name == name) {
			return pass.milliseconds;
		}
	}
	return 0.0f;
}
