#include "BakedLightingBake.h"
#include "BakedLightingBakeState.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>

// c++
#include <algorithm>
#include <chrono>
#include <stdexcept>

//============================================================================
//	BakedLightingBake classMethods
//============================================================================
Engine::BakedLightingBake::BakedLightingBake() = default;
Engine::BakedLightingBake::~BakedLightingBake() { Release(); }

Engine::BakedLightingBakeState::~BakedLightingBakeState() {

	if (readback) retirement->Retire(readback);
	if (probeReadback) retirement->Retire(probeReadback);
}

void Engine::BakedLightingBake::Release() {

	if (!state_) return;
	// Workerが参照する入力を破棄前に回収する
	state_->cancelled.store(true);
	if (state_->worker.valid()) state_->worker.wait();
	state_.reset();
}

void Engine::BakedLightingBake::Start(BakedLightingInput input, const BakedLightingSettings& settings,
	const AssetDatabase& database) {

	if (IsRunning()) throw std::logic_error("ベイクは既に実行中です");
	Release();
	state_ = std::make_unique<BakedLightingBakeState>();
	auto& state = *state_;
	state.input = std::move(input);
	state.database = std::make_unique<AssetDatabase>(database);
	state.settings = settings;
	state.settings.sampleCount = std::clamp(settings.sampleCount, 16u, 4096u);
	state.settings.bounceCount = std::clamp(settings.bounceCount, 1u, 8u);
	state.settings.texelsPerFrame = std::clamp(settings.texelsPerFrame, 256u, 8192u);
	state.settings.atlasSize = std::clamp(settings.atlasSize, 64u, 2048u);
	state.settings.captureSize = std::clamp(settings.captureSize, 16u, 128u);
	state.running = true;
	state.status = "ベイク用UVを生成中";
	state.worker = std::async(std::launch::async, [&state]() {
		// 入力のHashを計算し、外部更新を保存前に検出する
		nlohmann::json revisions = nlohmann::json::array();
		for (const auto& [id, path] : state.input.assets) {
			if (state.cancelled.load()) return;
			const auto* metadata = state.database->Find(id);
			if (!metadata) throw std::runtime_error("ベイクの入力Assetが見つかりません");
			revisions.push_back({{"assetId", ToAssetReferenceJson(id)}, {"hash", ContentHash::FileSHA256(path)},
				{"importer", metadata->importer}, {"importerVersion", metadata->importerVersion},
				{"importerSettings", metadata->importerSettings}});
		}
		state.input.preparation.data.inputRevisions["assets"] = std::move(revisions);
		BakedLightingGeometry::Prepare(state.input.preparation, state.settings, state.cancelled, state.preparationProgress);
		if (!state.cancelled.load()) PrepareBakedLightingVolumes(state);
	});
}

void Engine::BakedLightingBake::Cancel() {

	if (state_) state_->cancelled.store(true);
}

void Engine::BakedLightingBake::Tick(GraphicsCore& graphics) try {

	if (!state_ || !state_->running) return;
	auto& state = *state_;
	if (state.worker.valid()) {
		if (state.worker.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
		state.worker.get();
		if (state.filtering) {
			if (!state.cancelled.load()) state.result = std::make_shared<BakedLightingData>(std::move(state.input.preparation.data));
			state.running = false;
			state.status = state.cancelled.load() ? "ベイクを中止しました" : "ベイクが完了しました";
			// 完了結果以外のCPU作業データを解放する
			state.input = {};
			state.database.reset();
			return;
		}
		state.prepared = true;
	}
	if (state.cancelled.load()) {
		ReleaseBakedLightingGPU(state);
		state.running = false;
		state.status = "ベイクを中止しました";
		state.input = {};
		state.database.reset();
		return;
	}
	if (!state.prepared) return;
	if (!state.initialized) {
		state.status = "GPU資源とTextureを準備中";
		if (!InitializeBakedLightingGPU(graphics, state)) return;
		state.initialized = true;
	}
	if (state.waitingReadback) {
		if (!ReadBakedLightingGPU(graphics, state)) return;
		ReleaseBakedLightingGPU(state);
		// 平滑化中はGPUの作業を増やさない
		state.filtering = true;
		state.status = "LightmapとCaptureを平滑化中";
		state.worker = std::async(std::launch::async, [&state]() {
			BakedLightingGeometry::FilterLightmaps(state.input.preparation.data, state.input.preparation.texels, state.cancelled);
			FilterBakedReflectionCaptures(state.input.preparation.data, state.cancelled);
			if (!state.cancelled.load()) BakedLightingInputBuilder::ValidateAssets(state.input.preparation.data, *state.database, true);
		});
		return;
	}
	state.status = state.stage == 0 ? "間接光を計算中" : state.stage == 1 ? "Light Probesを計算中" : "Reflection Captureを計算中";
	DispatchBakedLightingGPU(graphics, state);
} catch (const std::exception& error) {
	ReleaseBakedLightingGPU(*state_);
	state_->running = false;
	state_->status = error.what();
	state_->input = {};
	state_->database.reset();
}

bool Engine::BakedLightingBake::IsRunning() const {

	return state_ && state_->running;
}

float Engine::BakedLightingBake::GetProgress() const {

	if (!state_) return 0.0f;
	if (state_->result) return 1.0f;
	if (!state_->prepared) return state_->preparationProgress.load() * 0.15f;
	if (state_->filtering) return 0.95f;
	return 0.15f + 0.8f * (float(state_->stage) + float(state_->sampleStart) / float(state_->settings.sampleCount)) / 3.0f;
}

std::string Engine::BakedLightingBake::GetStatus() const {

	return state_ ? state_->status : std::string{};
}

std::shared_ptr<const Engine::BakedLightingData> Engine::BakedLightingBake::GetResult() const {

	return state_ ? state_->result : nullptr;
}
