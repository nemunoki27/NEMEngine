#include "EditorRenderBenchmark.h"
#include "EngineApplication.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>

// c++
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <psapi.h>

namespace {

	nlohmann::json SummarizeTimes(std::vector<double>& values) {

		// frameごとの値から停止の長さと頻度を残す
		std::sort(values.begin(), values.end());
		const auto percentile = [&](double ratio) {
			return values[static_cast<size_t>(ratio * static_cast<double>(values.size() - 1))];
		};
		return { {"count", values.size()}, {"mean", std::accumulate(values.begin(), values.end(), 0.0) / values.size()},
			{"median", percentile(0.5)}, {"p95", percentile(0.95)}, {"p99", percentile(0.99)}, {"max", values.back()} };
	}
}

//============================================================================
//	EditorRenderBenchmark captureMethods
//============================================================================
void Engine::EditorRenderBenchmark::CaptureContinuousFrame(GraphicsCore& graphicsCore, float deltaTime) {

	auto& profiler = FrameProfiler::GetInstance();
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - phaseStarted_).count();
	if (captureFirstCPUFrameID_ == 0) captureFirstCPUFrameID_ = profiler.GetFrameID() - 1;
	captureTimes_["frameMs"].push_back(deltaTime * 1000.0);
	captureTimes_["updateMs"].push_back(profiler.GetLatestMs(FrameProfiler::Category::Update));
	captureTimes_["drawMs"].push_back(profiler.GetLatestMs(FrameProfiler::Category::Draw));
	captureTimes_["drawCPUMs"].push_back(profiler.GetLatestMs(FrameProfiler::Category::DrawCPU));
	captureTimes_["presentCPUMs"].push_back(profiler.GetLatestMs(FrameProfiler::Category::PresentCPU));
	captureTimes_["fpsWaitMs"].push_back(profiler.GetLatestMs(FrameProfiler::Category::FPSWait));
	captureTimes_["gpuWaitMs"].push_back(profiler.GetLatestMs(FrameProfiler::Category::GPUWait));
	captureTimes_["commandCloseCPUMs"].push_back(profiler.GetLatestMs(FrameProfiler::Category::CommandCloseCPU));
	captureTimes_["commandSubmitCPUMs"].push_back(profiler.GetLatestMs(FrameProfiler::Category::CommandSubmitCPU));
	captureTimes_["bufferSubmitCPUMs"].push_back(profiler.GetLatestMs(FrameProfiler::Category::BufferSubmitCPU));
	captureTimes_["uploadContextWaitMs"].push_back(profiler.GetLatestMs(FrameProfiler::Category::UploadContextWaitCPU));
	const auto& rendering = profiler.GetRenderingStatistics();
	captureTimes_["barrierCalls"].push_back(rendering.barrierCallCount);
	captureTimes_["transitionBarriers"].push_back(rendering.transitionBarrierCount);
	captureTimes_["uavBarriers"].push_back(rendering.uavBarrierCount);
	captureTimes_["graphicsSubmissions"].push_back(rendering.graphicsSubmissionCount);
	captureTimes_["bufferSubmissions"].push_back(rendering.bufferSubmissionCount);
	captureTimes_["meshTransferBytes"].push_back(static_cast<double>(rendering.meshTransferBytes));
	captureTimes_["meshRebuilds"].push_back(rendering.meshRebuildCount);
	captureTimes_["tlasBuilds"].push_back(rendering.tlasBuildCount);
	captureTimes_["tlasRefits"].push_back(rendering.tlasRefitCount);

	// GPU結果は元frameを保持し重複取得を数えない
	if (profiler.HasGPUData() && capturedGPUFrameID_ != profiler.GetGPUFrameID()) {

		capturedGPUFrameID_ = profiler.GetGPUFrameID();
		if (captureFirstGPUFrameID_ == 0) captureFirstGPUFrameID_ = capturedGPUFrameID_;
		captureTimes_["gpuMs"].push_back(profiler.GetGPUTotalMs());
		for (const auto& pass : profiler.GetGPUPassTimes()) {
			captureTimes_[pass.viewID + "|" + pass.name].push_back(pass.milliseconds);
		}
		if (profiler.GetGPUTotalMs() >= 50.0f) {
			report_["stallCount"] = report_.value("stallCount", uint64_t(0)) + 1;

			// 詳細な停止frameは末尾二百五十六件だけ残す
			auto& stalls = report_["stalls"];
			if (stalls.is_null()) stalls = nlohmann::json::array();
			if (stalls.size() >= 256) stalls.erase(stalls.begin());
			nlohmann::json stall = { {"elapsedSeconds", seconds}, {"gpuFrameID", capturedGPUFrameID_},
				{"gpuMs", profiler.GetGPUTotalMs()}, {"passes", nlohmann::json::array()} };
			for (const auto& pass : profiler.GetGPUPassTimes()) {
				stall["passes"].push_back({ {"name", pass.name}, {"ms", pass.milliseconds}, {"viewID", pass.viewID} });
			}
			stalls.push_back(std::move(stall));
		}
	}
	if (seconds < static_cast<double>(captureBucket_ + 1) && seconds < holdSeconds_) return;

	// 大きい資源集計は一秒に一度だけ行う
	nlohmann::json sample = { {"elapsedSeconds", seconds}, {"firstCPUFrameID", captureFirstCPUFrameID_},
		{"lastCPUFrameID", profiler.GetFrameID() - 1}, {"firstGPUFrameID", captureFirstGPUFrameID_},
		{"lastGPUFrameID", capturedGPUFrameID_}, {"gpuValid", captureTimes_.contains("gpuMs")},
		{"passes", nlohmann::json::array()}, {"distributions", nlohmann::json::object()},
		{"assetsReady", application_->AreSceneMeshesReady() && !graphicsCore.GetTextureUploadService().HasPendingRequests()} };
	for (auto& [name, values] : captureTimes_) {

		const auto distribution = SummarizeTimes(values);
		const size_t separator = name.find('|');
		if (separator == std::string::npos) {
			sample[name] = distribution.at("mean");
			sample["distributions"][name] = distribution;
		} else {
			sample["passes"].push_back({ {"name", name.substr(separator + 1)}, {"viewID", name.substr(0, separator)},
				{"ms", distribution.at("mean")}, {"distribution", distribution} });
		}
	}
	auto& platform = graphicsCore.GetDXObject();
	Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
	DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
	sample["gpuMemoryValid"] = SUCCEEDED(platform.GetDxgiFactory()->EnumAdapterByLuid(
		platform.GetDevice()->GetAdapterLuid(), IID_PPV_ARGS(&adapter))) &&
		SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory));
	sample["gpuMemoryUsage"] = memory.CurrentUsage;
	sample["gpuMemoryBudget"] = memory.Budget;
	sample["srvDescriptors"] = graphicsCore.GetSRVDescriptor().GetUseDescriptorCount();
	sample["retirementPending"] = platform.GetResourceRetirement().GetPendingCount();
	// OSの集計値を取得しCPUメモリの増加も残す
	PROCESS_MEMORY_COUNTERS_EX process{};
	process.cb = sizeof(process);
	sample["processMemoryValid"] = K32GetProcessMemoryInfo(GetCurrentProcess(),
		reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process), sizeof(process)) != FALSE;
	sample["privateBytes"] = process.PrivateUsage;
	sample["workingSetBytes"] = process.WorkingSetSize;
	sample["peakWorkingSetBytes"] = process.PeakWorkingSetSize;
	const auto& uploads = graphicsCore.GetBufferUploadService().GetStatistics();
	sample["bufferUploads"] = {{"uploadedBytes", uploads.uploadedBytes}, {"uploadCount", uploads.uploadCount},
		{"stagingCreationCount", uploads.stagingCreationCount}, {"pooledBytes", uploads.pooledBytes}};
	const auto textures = graphicsCore.GetTextureUploadService().GetCacheStatistics();
	sample["textures"] = { {"count", textures.textureCount}, {"allocationBytes", textures.allocationBytes},
		{"queuedCount", textures.queuedCount}, {"decodedBytes", textures.decodedBytes}, {"uploadingCount", textures.uploadingCount} };
	if (frameLog_.is_open()) {
		frameLog_ << sample.dump() << '\n';
		if (!frameLog_) throw std::runtime_error("連続計測標本を書き込めません");
	}
	frames_.push_back(std::move(sample));
	++capturedIntervalCount_;
	if (frameLog_.is_open() && frames_.size() > 30u) frames_.erase(frames_.begin());
	captureTimes_.clear();
	captureFirstCPUFrameID_ = 0;
	captureFirstGPUFrameID_ = 0;
	captureBucket_ = static_cast<uint32_t>(seconds);
	if (seconds >= holdSeconds_) {

		// 計測が終わってから画像の読戻しを行う
		FinishPhase(graphicsCore);
		captureBucket_ = 0;
		return;
	}
	if (captureBucket_ % 30 == 0) {

		// 中断時にも直近の経過を確認できるよう保存する
		if (frameLog_.is_open()) frameLog_.flush();
		const size_t first = frames_.size() > 30 ? frames_.size() - 30 : 0;
		report_["continuousFrames"] = nlohmann::json(frames_.begin() + first, frames_.end());
		report_["intervalCount"] = capturedIntervalCount_;
		if (!JsonFile::Save(output_, report_)) throw std::runtime_error("連続計測結果を保存できません");
	}
}
