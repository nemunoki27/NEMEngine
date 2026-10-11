#include "EditorRenderBenchmark.h"
#include "EngineApplication.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetNames.h>
#include <algorithm>
#include <stdexcept>
#include <Externals/DirectXTex/DirectXTex.h>

//============================================================================
//	EditorRenderBenchmark classMethods
//============================================================================
Engine::EditorRenderBenchmark::EditorRenderBenchmark(std::unique_ptr<EngineApplication> application,
	std::filesystem::path output) : application_(std::move(application)), output_(std::move(output)) {
}

Engine::EditorRenderBenchmark::~EditorRenderBenchmark() = default;

void Engine::EditorRenderBenchmark::Init(GraphicsCore& graphicsCore) {

	features_ = &graphicsCore.GetDXObject().GetFeatureController();
	originalMeshShader_ = features_->GetPreferences().allowMeshShader;
	application_->Init(graphicsCore);
	wchar_t freeze[2]{};
	freezeAnimation_ = GetEnvironmentVariableW(L"NEM_RENDER_BENCHMARK_FREEZE", freeze, 2) == 1 && freeze[0] == L'1';
	// 入力とウィンドウ移動による条件変化を避ける
	ShowWindow(WinApp::GetHwnd(), SW_HIDE);
	const auto& settings = graphicsCore.GetContext().GetWindowSetting();
	report_ = { { "adapter", features_->GetAdapterInfo().adapterName },
		{ "width", settings.gameSize.x }, { "height", settings.gameSize.y },
		{ "freezeAnimation", freezeAnimation_ },
		{ "cpuSamples", "FrameProfiler rolling category averages" },
		{ "gpuSamples", "latest completed GPU frame, no additional wait" },
		{ "phases", nlohmann::json::array() } };
	BeginPhase();
}

void Engine::EditorRenderBenchmark::BeginPhase() {

	// ON→OFF→ONでキャッシュと熱状態の片寄りも確認する
	features_->SetAllowMeshShader(phase_ != 1);
	warmFrames_ = 0;
	frames_ = nlohmann::json::array();
	phaseStarted_ = std::chrono::steady_clock::now();
	Logger::Output(LogType::Engine, spdlog::level::info,
		"描画比較を開始します phase={} MeshShader={}", phase_, features_->ShouldUseMeshShader());
}

void Engine::EditorRenderBenchmark::Tick(GraphicsCore& graphicsCore, float deltaTime) {

	application_->Tick(graphicsCore, freezeAnimation_ ? 0.0f : deltaTime);
	if (phase_ >= 3) {
		return;
	}
	const uint64_t revision = graphicsCore.GetTextureUploadService().GetContentRevision();
	if (revision != textureRevision_) {
		textureRevision_ = revision;
		warmFrames_ = 0;
		frames_ = nlohmann::json::array();
	}
	const auto elapsed = std::chrono::steady_clock::now() - phaseStarted_;
	// 読込後180フレームと開始後30秒を計測対象から外す
	if (++warmFrames_ <= 180 || elapsed < std::chrono::seconds(30)) {
		return;
	}
	auto& profiler = FrameProfiler::GetInstance();
	nlohmann::json sample = { { "frameMs", deltaTime * 1000.0f },
		{ "updateMs", profiler.GetAverageMs(FrameProfiler::Category::Update) },
		{ "drawMs", profiler.GetAverageMs(FrameProfiler::Category::Draw) },
		{ "gpuWaitMs", profiler.GetAverageMs(FrameProfiler::Category::GPUWait) },
		{ "meshBuildMs", profiler.GetAverageMs(FrameProfiler::Category::MeshBatchBuild) },
		{ "meshMaterialMs", profiler.GetAverageMs(FrameProfiler::Category::MeshMaterialBuild) },
		{ "meshTransferBytes", profiler.GetRenderingStatistics().meshTransferBytes },
		{ "gpuValid", profiler.HasGPUData() }, { "gpuMs", profiler.GetGPUTotalMs() },
		{ "passes", nlohmann::json::array() } };
	for (const auto& pass : profiler.GetGPUPassTimes()) {
		sample["passes"].push_back({ { "name", pass.name }, { "ms", pass.milliseconds } });
	}
	frames_.push_back(std::move(sample));
	if (frames_.size() == 180) {
		FinishPhase(graphicsCore);
	}
}

void Engine::EditorRenderBenchmark::FinishPhase(GraphicsCore& graphicsCore) {

	// 空SceneやGPU計測の欠落を性能結果として採用しない
	if (std::none_of(frames_.begin(), frames_.end(), [](const auto& frame) {
		return frame.at("gpuValid").template get<bool>() && !frame.at("passes").empty();
	})) {
		throw std::runtime_error("描画比較に有効なGPU計測がありません");
	}
	report_["phases"].push_back({ { "phase", phase_ },
		{ "meshShader", features_->ShouldUseMeshShader() }, { "frames", std::move(frames_) } });
	if (!JsonFile::Save(output_, report_)) {
		throw std::runtime_error("描画比較結果を保存できません");
	}
	CaptureViews(graphicsCore);
	if (++phase_ < 3) {
		BeginPhase();
	} else {
		// 確認専用プロセスを通常の終了処理へ進める
		PostQuitMessage(0);
	}
}

void Engine::EditorRenderBenchmark::CaptureViews(GraphicsCore& graphicsCore) {

	// 画像の読戻し待機は計測区間の外へ置く
	for (const auto kind : { RenderViewKind::Game, RenderViewKind::Scene }) {
		for (const char* attachment : { "", RenderTargetNames::kSceneColorMain, RenderTargetNames::kSceneNormalMain,
			RenderTargetNames::kScenePositionMain, RenderTargetNames::kSceneMaterialMain }) {
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
			if (image->format != DXGI_FORMAT_R8G8B8A8_UNORM) {
				if (FAILED(DirectX::Convert(*image, DXGI_FORMAT_R8G8B8A8_UNORM,
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
			if (*attachment) {
				// 法線と座標は丸めずに数値比較できる形式でも残す
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

void Engine::EditorRenderBenchmark::Render(GraphicsCore& graphicsCore) {

	application_->Render(graphicsCore);
}

void Engine::EditorRenderBenchmark::RenderPlatformWindows(GraphicsCore& graphicsCore) {

	application_->RenderPlatformWindows(graphicsCore);
}

void Engine::EditorRenderBenchmark::Finalize() {

	if (features_) {
		features_->SetAllowMeshShader(originalMeshShader_);
	}
	application_->Finalize();
}

bool Engine::EditorRenderBenchmark::ConsumeFrameDeltaResetRequest() {

	return application_->ConsumeFrameDeltaResetRequest();
}
