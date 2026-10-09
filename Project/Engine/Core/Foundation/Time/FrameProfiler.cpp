#include "FrameProfiler.h"

//============================================================================
//	include
//============================================================================
// c++
#include <utility>

//============================================================================
//	FrameProfiler classMethods
//============================================================================
Engine::FrameProfiler::FrameProfiler() = default;

Engine::FrameProfiler::~FrameProfiler() = default;

Engine::FrameProfiler& Engine::FrameProfiler::GetInstance() {

	static FrameProfiler instance;
	return instance;
}

void Engine::FrameProfiler::BeginFrame(float deltaTimeSec, float totalTimeSec) {

	++frameID_;
	deltaTimeSec_ = deltaTimeSec;
	totalTimeSec_ = totalTimeSec;

	// 前フレームの累積を履歴へ確定し、今フレームの累積をリセットする
	if (enabled_) {
		for (FrameProfileHistory& measure : measures_) {
			measure.BeginFrame(firstFrame_);
		}
	}
	firstFrame_ = false;
	resolvedRenderingStatistics_ = renderingStatistics_;
	renderingStatistics_ = {};
}

void Engine::FrameProfiler::AddSample(Category category, float milliseconds) {

	const size_t index = static_cast<size_t>(category);
	if (!enabled_ || index >= measures_.size()) {
		return;
	}
	measures_[index].Add(milliseconds);
}

void Engine::FrameProfiler::SetGPUPassTimes(const std::vector<NamedTime>& passes) {

	gpuPassTimes_ = passes;
}

void Engine::FrameProfiler::SetEnabled(bool enabled) {

	// 計測状態を切り替える
	if (enabled_ == enabled) { return; }
	enabled_ = enabled;
	// 再開前の途中計測を次の履歴へ持ち越さない
	for (FrameProfileHistory& measure : measures_) { measure.BeginFrame(true); }
	firstFrame_ = true;
}

void Engine::FrameProfiler::SetGPUFrame(uint64_t frameID, const std::vector<NamedTime>& passes, std::string_view status) {

	// 遅れて取得したGPU結果のフレームを保持する
	gpuFrameID_ = frameID;
	gpuStatus_ = status;
	gpuPassTimes_ = passes;
}

void Engine::FrameProfiler::SetECSSystemTimes(const std::vector<NamedTime>& systems) {

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
	return index < measures_.size() ? measures_[index].GetAverage() : 0.0f;
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

//============================================================================
//	FrameProfiler classMethods
//============================================================================

namespace Engine {

	FrameProfiler::ScopedSample::ScopedSample(Category category) : category_(category) {

		// 通常実行では時計の取得を省く
		enabled_ = FrameProfiler::GetInstance().IsEnabled();
		if (enabled_) { start_ = std::chrono::high_resolution_clock::now(); }
	}

	FrameProfiler::ScopedSample::~ScopedSample() {

		if (!enabled_) { return; }
		const std::chrono::duration<float, std::milli> elapsed =
			std::chrono::high_resolution_clock::now() - start_;
		FrameProfiler::GetInstance().AddSample(category_, elapsed.count());
	}
}
