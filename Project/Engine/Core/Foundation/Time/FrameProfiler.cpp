#include "FrameProfiler.h"
#include "ProfileCapture.h"

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

	// 前フレームのCPU結果と描画条件を同じ記録へ確定する
	if (!firstFrame_ && enabled_ && IsCaptureRecording() && captureStartFrame_ <= frameID_) {
		capture_->Append(frameID_, { { "conditions", nlohmann::json::parse(conditions_) }, { "cpuMilliseconds", frameSamples_ },
			{ "cpuCategories", { "Update", "ECS", "Script", "Draw", "GPUWait", "MeshBatchUpload", "MeshBatchBuild",
				"MeshBufferTransfer", "MeshMaterialBuild" } },
			{ "script", nlohmann::json::parse(scriptFrame_) },
			{ "deltaTime", deltaTimeSec_ }, { "totalTime", totalTimeSec_ },
			{ "meshTransferBytes", renderingStatistics_.meshTransferBytes },
			{ "queuedFrameCount", renderingStatistics_.queuedFrameCount } });
	}
	++frameID_;
	scriptFrame_ = "{}";
	frameSamples_.fill(0.0f);
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
	frameSamples_[index] += milliseconds;
}

void Engine::FrameProfiler::SetGPUPassTimes(const std::vector<NamedTime>& passes) {

	gpuPassTimes_ = passes;
}

void Engine::FrameProfiler::SetConditions(std::string conditions) {

	conditions_ = std::move(conditions);
}

Engine::ProfileCapture& Engine::FrameProfiler::GetCapture() {

	// 通常の製品実行では記録用データを確保しない
	if (!capture_) { capture_ = std::make_unique<ProfileCapture>(); }
	return *capture_;
}

bool Engine::FrameProfiler::IsCaptureRecording() const {

	return capture_ && capture_->IsRecording();
}

bool Engine::FrameProfiler::StartCapture(uint32_t frameLimit) {

	if (!enabled_ || !GetCapture().Start(frameLimit)) { return false; }
	// 操作途中のフレームは記録へ含めない
	captureStartFrame_ = frameID_ + 1;
	++captureRevision_;
	return true;
}

void Engine::FrameProfiler::SkipCaptureFrame() {

	if (IsCaptureRecording()) { captureStartFrame_ = frameID_ + 1; }
}

void Engine::FrameProfiler::SetEnabled(bool enabled) {

	// 計測を無効にした後も未完了の記録を残さない
	if (enabled_ == enabled) { return; }
	enabled_ = enabled;
	if (!enabled && capture_) { capture_->Stop("profiling_disabled"); }
	// 再開前の途中計測を次の履歴へ持ち越さない
	for (FrameProfileHistory& measure : measures_) { measure.BeginFrame(true); }
	firstFrame_ = true;
}

void Engine::FrameProfiler::SetScriptFrame(std::string snapshot) {

	scriptFrame_ = std::move(snapshot);
}

void Engine::FrameProfiler::SetGPUFrame(uint64_t frameID, const std::vector<NamedTime>& passes, std::string_view status) {

	// 表示用の最新結果と記録用の対応先を分ける
	gpuFrameID_ = frameID;
	gpuStatus_ = status;
	gpuPassTimes_ = passes;
	if (!capture_ || !capture_->GetSnapshot().is_object()) { return; }
	const auto& frames = capture_->GetSnapshot()["frames"];
	// 記録範囲外のGPU結果にはJSONを作らない
	if (frames.empty() || frameID < frames.front()["frameID"].get<uint64_t>() ||
		frames.back()["frameID"].get<uint64_t>() < frameID) { return; }
	nlohmann::json values = nlohmann::json::array();
	for (const auto& pass : passes) {
		values.push_back({ { "name", pass.name }, { "viewID", pass.viewID }, { "milliseconds", pass.milliseconds } });
	}
	capture_->AttachGPU(frameID, values, status);
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
