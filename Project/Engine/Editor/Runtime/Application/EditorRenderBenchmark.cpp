#include "EditorRenderBenchmark.h"
#include "EngineApplication.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetNames.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/CameraSurfacePipeline.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/RenderFeatureResourceUtility.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Rendering/SkyboxRendererComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Editor/Utility/AssetEntityFactory.h>
#include <Engine/Editor/Commands/Transform/SetTransformCommand.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
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
	// 短い確認は性能比較と区別して記録
	wchar_t quick[2]{};
	quickCheck_ = GetEnvironmentVariableW(L"NEM_RENDER_BENCHMARK_QUICK", quick, 2) == 1 && quick[0] == L'1';
	wchar_t continuous[2]{};
	continuousCapture_ = GetEnvironmentVariableW(L"NEM_RENDER_BENCHMARK_CONTINUOUS", continuous, 2) == 1 && continuous[0] == L'1';
	// Debug Layerの停止前に原因をログへ残す
	if (SUCCEEDED(graphicsCore.GetDXObject().GetDevice()->QueryInterface(IID_PPV_ARGS(&diagnostics_)))) {

		diagnostics_->RegisterMessageCallback([](D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity,
			D3D12_MESSAGE_ID id, LPCSTR text, void*) {

			if (severity > D3D12_MESSAGE_SEVERITY_ERROR) return;
			Logger::Output(LogType::Engine, spdlog::level::err, "D3D12 ERROR id={} {}", static_cast<uint32_t>(id), text);
		}, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &diagnosticsCookie_);
	}
	application_->Init(graphicsCore);
	// 長時間確認とScene反復は専用プロセスだけで行う
	wchar_t setting[32]{};
	if (GetEnvironmentVariableW(L"NEM_RENDER_BENCHMARK_PHASE_COUNT", setting, 32) > 0u) {
		phaseCount_ = static_cast<uint32_t>(std::clamp(std::wcstoul(setting, nullptr, 10), 1ul, 128ul));
	}
	if (GetEnvironmentVariableW(L"NEM_RENDER_BENCHMARK_HOLD_SECONDS", setting, 32) > 0u) {
		holdSeconds_ = static_cast<uint32_t>(std::min(std::wcstoul(setting, nullptr, 10), 3600ul));
	}
	if (continuousCapture_ && holdSeconds_ == 0) holdSeconds_ = 900;
	const auto scenes = Algorithm::GetEnvironmentPath(L"NEM_RENDER_BENCHMARK_SCENES");
	if (!scenes.empty()) {

		sceneSequence_ = JsonFile::Load(scenes, false);
		if (!sceneSequence_.is_array() || sceneSequence_.empty()) throw std::runtime_error("Scene反復の指定が不正です");
	}
	wchar_t freeze[2]{};
	freezeAnimation_ = GetEnvironmentVariableW(L"NEM_RENDER_BENCHMARK_FREEZE", freeze, 2) == 1 && freeze[0] == L'1';
	// 入力とウィンドウ移動による条件変化を避ける
	if (GetEnvironmentVariableW(L"NEM_RENDER_BENCHMARK_VISIBLE", continuous, 2) != 1 || continuous[0] != L'1') {
		ShowWindow(WinApp::GetHwnd(), SW_HIDE);
	}
	const auto& settings = graphicsCore.GetContext().GetWindowSetting();
	bool gpuValidation = false;
#if defined(_DEBUG)
	gpuValidation = quick[0] == L'1';
#endif
	report_ = { { "adapter", features_->GetAdapterInfo().adapterName },
		{ "width", settings.gameSize.x }, { "height", settings.gameSize.y },
		{ "freezeAnimation", freezeAnimation_ },
		{ "comparison", sceneSequence_.empty() ? "MeshShader" : "SceneSwitch" },
		{ "quickCheck", quickCheck_ }, { "gpuValidation", gpuValidation },
		{ "phaseCount", phaseCount_ }, { "holdSeconds", holdSeconds_ }, { "sceneSequence", sceneSequence_ },
		{ "cpuSamples", "FrameProfiler rolling category averages" },
		{ "gpuSamples", "latest completed GPU frame, no additional wait" },
		{ "phases", nlohmann::json::array() } };
	if (continuousCapture_) report_["cpuSamples"] = "completed CPU frame distributions, GPU frame IDs recorded separately";
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
	BeginPhase();
}

void Engine::EditorRenderBenchmark::BeginPhase() {

	if (!sceneSequence_.empty()) {

		const std::string path = sceneSequence_[phase_ % sceneSequence_.size()].get<std::string>();
		const auto* meta = application_->assetDatabase_.FindByPath(path);
		if (!meta) throw std::runtime_error("反復対象のSceneがありません: " + path);
		sceneReadStarted_ = std::chrono::steady_clock::now();
		scenePreparationMaxTickMs_ = 0.0;
		application_->QueueEditScene(meta->guid);
		if (!application_->pendingSceneRead_.valid()) throw std::runtime_error("Sceneの準備を開始できません: " + path);
		sceneReadPending_ = true;
		report_["cameraMotionOrigins"] = nlohmann::json::object();
	}

	// 同じSceneで機能の切替前後を計測
	features_->SetAllowMeshShader(sceneSequence_.empty() && !report_.contains("ambientOcclusionSequence") &&
		!report_.contains("shadowSequence") && !report_.contains("transformSequence") ? phase_ != 1 : originalMeshShader_);
	warmFrames_ = 0;
	frames_ = nlohmann::json::array();
	phaseStarted_ = std::chrono::steady_clock::now();
	Logger::Output(LogType::Engine, spdlog::level::info,
		"描画比較を開始します phase={} MeshShader={}", phase_, features_->ShouldUseMeshShader());
}

void Engine::EditorRenderBenchmark::Tick(GraphicsCore& graphicsCore, float deltaTime) {

	if (report_.contains("shadowSequence") && phase_ < phaseCount_) {

		const auto& settings = report_["shadowSequence"][phase_];
		features_->SetAllowInlineRayTracing(settings.value("enabled", true));
		features_->SetSoftShadowSampleCount(settings.value("samples", 4u));
		if (settings.contains("reflectionEnabled")) {
			if (auto* world = application_->GetActiveWorld()) {
				world->ForEach<PerspectiveCameraComponent>([&](Entity, PerspectiveCameraComponent& camera) {
					camera.common.reflectionEnabled = settings.at("reflectionEnabled").get<bool>();
				});
			}
		}
	}
	// 保存済みSceneを変えず実行中のCameraだけを変更する
	if (report_.contains("ambientOcclusionSequence") && phase_ < phaseCount_) {
		if (auto* world = application_->GetActiveWorld()) {
			const auto& settings = report_["ambientOcclusionSequence"][phase_];
			world->ForEach<PerspectiveCameraComponent>([&](Entity, PerspectiveCameraComponent& camera) {
				camera.common.ambientOcclusionEnabled = settings.at("enabled").get<bool>();
				camera.common.ambientOcclusionRadius = settings.at("radius").get<float>();
				camera.common.ambientOcclusionIntensity = settings.at("intensity").get<float>();
				camera.common.ambientOcclusionQuality = settings.value("quality", 1);
			});
			// 環境光だけの比較も隔離したWorldで行う
			if (settings.contains("iblIntensity")) {
				world->ForEach<SkyboxRendererComponent>([&](Entity, SkyboxRendererComponent& skybox) {
					skybox.iblIntensity = settings.at("iblIntensity").get<float>();
					skybox.color = Color4::White();
				});
			}
			if (settings.contains("directLightIntensity")) {
				world->ForEach<DirectionalLightComponent>([&](Entity, DirectionalLightComponent& light) {
					light.intensity = settings.at("directLightIntensity").get<float>();
				});
			}
		}
	}
	// 移動確認は隔離したEditorのGame Cameraだけへ適用
	wchar_t motion[2]{};
	if (GetEnvironmentVariableW(L"NEM_RENDER_BENCHMARK_CAMERA_MOTION", motion, 2) == 1 && motion[0] == L'1') {
		if (auto* world = application_->GetActiveWorld()) {
			world->ForEach<PerspectiveCameraComponent, TransformComponent>([&](Entity entity,
				PerspectiveCameraComponent& camera, TransformComponent& transform) {

				if (!camera.common.enabled) return;
				const std::string key = std::to_string(entity.index);
				auto& origins = report_["cameraMotionOrigins"];
				if (!origins.contains(key)) origins[key] = transform.localPos.x;
				const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - phaseStarted_).count();
				const float position = origins[key].get<float>() + (phase_ == 1u ? 0.5f * std::sin(float(seconds) * 1.5f) : 0.0f);
				if (transform.localPos.x != position) {
					transform.localPos.x = position;
					MarkTransformSubtreeDirty(*world, entity);
				}
			});
		}
	}
	const auto tickStarted = std::chrono::steady_clock::now();
	const bool preparingScene = sceneReadPending_ && application_->pendingSceneRead_.valid() &&
		application_->pendingSceneRead_.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
	application_->Tick(graphicsCore, freezeAnimation_ ? 0.0f : deltaTime);
	// Editorの操作受付を開始してから変更を確定する
	if (report_.contains("transformSequence") && phase_ < phaseCount_ &&
		report_.value("appliedTransformPhase", UINT32_MAX) != phase_) {
		if (auto* world = application_->GetActiveWorld(); world && application_->editScenes_.GetActive()) {

			const auto& settings = report_["transformSequence"][phase_];
			const uint32_t motionFrames = settings.value("motionFrames", 0u);
			auto& updates = report_["transformUpdates"][std::to_string(phase_)];
			const uint32_t update = updates.is_number_unsigned() ? updates.get<uint32_t>() : 0u;
			const float progress = motionFrames > 0 ? std::min(float(update) / motionFrames, 1.0f) : 1.0f;
			// 実際のドラッグ配置と同じ生成処理を使う
			for (const auto& target : settings.at("targets")) {

				const std::string path = target.at("asset").get<std::string>();
				auto& reference = report_["transformTargets"][path];
				if (reference.is_null()) {

					const auto* meta = application_->assetDatabase_.FindByPath(path);
					if (!meta) throw std::runtime_error("SRT比較のAssetがありません: " + path);
					EditorAssetDragDropPayload payload;
					payload.assetID = meta->guid;
					payload.assetType = meta->type;
					strcpy_s(payload.assetPath, path.c_str());
					HierarchySystem hierarchy;
					const auto spawn = AssetEntityFactory::Spawn(*world, application_->assetDatabase_, graphicsCore,
						hierarchy, payload, application_->editScenes_.GetActive()->instanceID);
					if (!spawn.valid) throw std::runtime_error("SRT比較のEntityを生成できません");
					reference = { {"index", spawn.root.index}, {"generation", spawn.root.generation} };
				}
				const Entity entity{reference.at("index").get<uint32_t>(), reference.at("generation").get<uint32_t>()};
				auto transform = world->GetComponent<TransformComponent>(entity);
				const auto readVector = [](const nlohmann::json& value) {
					return Vector3(value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>());
				};
				transform.localPos = readVector(target.at("position"));
				// 連続操作で更新した探索構造も比較する
				transform.localPos.x += std::sin(progress * 6.2831853f) * 100.0f;
				transform.localScale = readVector(target.at("scale"));
				transform.localRotation = Quaternion::EulerToQuaternion(readVector(target.at("rotation")));
				const auto& before = world->GetComponent<TransformComponent>(entity);
				if (!SetTransformCommand::NearlyEqualTransform(before, transform) &&
					!application_->editorManager_.ExecuteEditorCommand(std::make_unique<SetTransformCommand>(entity, before, transform))) {
					throw std::runtime_error("SRT比較の編集を確定できません");
				}
			}
			updates = update + 1u;
			if (update >= motionFrames) report_["appliedTransformPhase"] = phase_;
		}
	}
	if (sceneReadPending_) {

		const double tickMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tickStarted).count();
		if (preparingScene) scenePreparationMaxTickMs_ = (std::max)(scenePreparationMaxTickMs_, tickMs);
		if (application_->pendingSceneRead_.valid()) return;
		const std::string path = sceneSequence_[phase_ % sceneSequence_.size()].get<std::string>();
		const auto* meta = application_->assetDatabase_.FindByPath(path);
		if (!meta || application_->activeScene_ != meta->guid) throw std::runtime_error("Sceneの非同期切替に失敗しました: " + path);
		report_["sceneLoads"].push_back({ { "phase", phase_ }, { "path", path },
			{ "seconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - sceneReadStarted_).count() },
			{ "preparationMaxTickMs", scenePreparationMaxTickMs_ }, { "commitTickMs", tickMs } });
		sceneReadPending_ = false;
		phaseStarted_ = std::chrono::steady_clock::now();
	}
	if (continuousCapture_ && phase_ < phaseCount_) {
		CaptureContinuousFrame(graphicsCore, deltaTime);
		return;
	}
	if (phase_ >= phaseCount_) {
		return;
	}
	// Meshと画像のGPU公開が終わってから安定待ちを始める
	if (!application_->AreSceneMeshesReady() || graphicsCore.GetTextureUploadService().HasPendingRequests()) {

		warmFrames_ = 0;
		frames_ = nlohmann::json::array();
		phaseStarted_ = std::chrono::steady_clock::now();
		return;
	}
	const uint64_t revision = graphicsCore.GetTextureUploadService().GetContentRevision();
	if (revision != textureRevision_) {
		textureRevision_ = revision;
		warmFrames_ = 0;
		frames_ = nlohmann::json::array();
		phaseStarted_ = std::chrono::steady_clock::now();
	}
	const auto elapsed = std::chrono::steady_clock::now() - phaseStarted_;
	// 読込後の安定待ちを計測対象から外す
	if (++warmFrames_ <= (quickCheck_ ? 30u : 180u) || elapsed < std::chrono::seconds(quickCheck_ ? 5 : 30)) {
		return;
	}
	auto& profiler = FrameProfiler::GetInstance();
	const double seconds = std::chrono::duration<double>(elapsed).count();
	if (holdSeconds_ > 0u && !frames_.empty() && seconds - frames_.back().at("elapsedSeconds").get<double>() < 1.0) return;
	nlohmann::json sample = { { "frameMs", deltaTime * 1000.0f },
		{ "elapsedSeconds", seconds },
		{ "updateMs", profiler.GetAverageMs(FrameProfiler::Category::Update) },
		{ "drawMs", profiler.GetAverageMs(FrameProfiler::Category::Draw) },
		{ "gpuWaitMs", profiler.GetAverageMs(FrameProfiler::Category::GPUWait) },
		{ "meshBuildMs", profiler.GetAverageMs(FrameProfiler::Category::MeshBatchBuild) },
		{ "meshMaterialMs", profiler.GetAverageMs(FrameProfiler::Category::MeshMaterialBuild) },
		{ "meshTransferBytes", profiler.GetRenderingStatistics().meshTransferBytes },
		{ "gpuValid", profiler.HasGPUData() }, { "gpuMs", profiler.GetGPUTotalMs() },
		{ "passes", nlohmann::json::array() } };
	// 描画時間とGPUメモリの圧迫を対応付ける
	auto& platform = graphicsCore.GetDXObject();
	Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
	DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
	const bool memoryValid = SUCCEEDED(platform.GetDxgiFactory()->EnumAdapterByLuid(
		platform.GetDevice()->GetAdapterLuid(), IID_PPV_ARGS(&adapter))) &&
		SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory));
	sample["gpuMemoryValid"] = memoryValid;
	sample["gpuMemoryUsage"] = memory.CurrentUsage;
	sample["gpuMemoryBudget"] = memory.Budget;
	sample["retirementPending"] = platform.GetResourceRetirement().GetPendingCount();
	sample["srvDescriptors"] = graphicsCore.GetSRVDescriptor().GetUseDescriptorCount();
	const auto& rendering = profiler.GetRenderingStatistics();
	sample["blasGeometries"] = rendering.blasGeometryCount;
	sample["tlasInstances"] = rendering.tlasInstanceCount;
	sample["blasBuilds"] = rendering.blasBuildCount;
	sample["tlasBuilds"] = rendering.tlasBuildCount;
	sample["tlasRefits"] = rendering.tlasRefitCount;
	sample["tlasSkips"] = rendering.tlasSkipCount;
	if (const auto* world = application_->GetActiveWorld()) {
		sample["entityRecords"] = world->GetRecordCount();
		sample["archetypes"] = world->GetArchetypeCount();
	}
	if (frames_.empty()) {

		const auto textures = graphicsCore.GetTextureUploadService().GetCacheStatistics(platform.GetDevice());
		sample["textures"] = { { "count", textures.textureCount }, { "allocationBytes", textures.allocationBytes },
			{ "queuedCount", textures.queuedCount }, { "decodedBytes", textures.decodedBytes } };
	}
	for (const auto& pass : profiler.GetGPUPassTimes()) {
		sample["passes"].push_back({ { "name", pass.name }, { "ms", pass.milliseconds },
			{ "viewID", pass.viewID }, { "frameID", pass.frameID } });
	}
	frames_.push_back(std::move(sample));
	if ((holdSeconds_ == 0u && frames_.size() == (quickCheck_ ? 30u : 180u)) ||
		(holdSeconds_ > 0u && seconds >= 30.0 + double(holdSeconds_))) {
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
		{ "meshShader", features_->ShouldUseMeshShader() },
		{ "frames", std::move(frames_) } });
	report_.erase("continuousFrames");
	if (sceneSequence_.empty() || phase_ == 0u || phase_ + 1u == phaseCount_) CaptureViews(graphicsCore);
	if (!JsonFile::Save(output_, report_)) {
		throw std::runtime_error("描画比較結果を保存できません");
	}
	if (++phase_ < phaseCount_) {
		BeginPhase();
	} else {
		// 確認専用プロセスを通常の終了処理へ進める
		PostQuitMessage(0);
	}
}

void Engine::EditorRenderBenchmark::CaptureViews(GraphicsCore& graphicsCore) {

	// 画像の読戻し待機は計測区間の外へ置く
	for (const auto kind : { RenderViewKind::Game, RenderViewKind::Scene }) {
		const std::string aoRaw = RenderFeatureResourceUtility::MakeOutputAlias({CameraSurfacePassIDs::AmbientOcclusionTrace, "Raw"});
		const std::string aoFiltered = RenderFeatureResourceUtility::MakeOutputAlias({CameraSurfacePassIDs::AmbientOcclusionFilter, "Color"});
		for (const char* attachment : { "", "InlineShadowRaw", "InlineShadowFiltered", aoRaw.c_str(), aoFiltered.c_str(), RenderTargetNames::kSceneColorMain, RenderTargetNames::kSceneNormalMain,
			RenderTargetNames::kScenePositionMain, RenderTargetNames::kSceneMaterialMain,
			RenderTargetNames::kSceneColorFinal, ViewportRenderService::GetPrimaryColorName(kind) }) {
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
			{
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
	if (diagnostics_) diagnostics_->UnregisterMessageCallback(diagnosticsCookie_);
	diagnostics_.Reset();
}

bool Engine::EditorRenderBenchmark::ConsumeFrameDeltaResetRequest() {

	return application_->ConsumeFrameDeltaResetRequest();
}
