#include "EditorRenderBenchmark.h"
#include "EngineApplication.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Editor/Commands/Entity/DeleteEntityCommand.h>
#include <Engine/Editor/Commands/Entity/EntityPropertyCommands.h>

// c++
#include <algorithm>
#include <cmath>
#include <stdexcept>

void Engine::EditorRenderBenchmark::ApplyObjectEdits() {

	auto& manager = application_->editorManager_;
	for (const auto& edit : report_.at("objectEdits").at(phase_)) {
		const std::string action = edit.at("action");
		bool applied = false;
		if (action == "undo") {
			applied = manager.UndoEditorCommand();
		} else if (action == "redo") {
			applied = manager.RedoEditorCommand();
		} else {
			const std::string name = edit.at("name");
			Entity target = Entity::Null();
			application_->GetActiveWorld()->ForEach<NameComponent>([&](Entity entity, const NameComponent& component) {
				if (component.name == name) target = entity;
			});
			if (target == Entity::Null()) throw std::runtime_error("対象のオブジェクトがありません: " + name);
			if (action == "delete") applied = manager.ExecuteEditorCommand(std::make_unique<DeleteEntityCommand>(target));
			if (action == "active") applied = manager.ExecuteEditorCommand(
				std::make_unique<SetEntityActiveCommand>(target, edit.at("value").get<bool>()));
		}
		if (!applied) throw std::runtime_error("オブジェクト操作に失敗しました: " + action);
	}
}

void Engine::EditorRenderBenchmark::InitComparisonConditions() {

	InitCaptureSettings();
	const auto edits = Algorithm::GetEnvironmentPath(L"NEM_RENDER_BENCHMARK_OBJECT_EDITS");
	if (!edits.empty()) {
		auto sequence = JsonFile::Load(edits, false);
		if (!sequence.is_array() || sequence.empty()) throw std::runtime_error("オブジェクト操作の指定が不正です");
		phaseCount_ = static_cast<uint32_t>(sequence.size());
		report_["comparison"] = "ObjectEdits";
		report_["phaseCount"] = phaseCount_;
		report_["objectEdits"] = std::move(sequence);
	}
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
	InitAntialiasingConditions();
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

void Engine::EditorRenderBenchmark::InitAntialiasingConditions() {

	const auto path = Algorithm::GetEnvironmentPath(L"NEM_RENDER_BENCHMARK_ANTIALIASING");
	if (path.empty()) return;
	if (!sceneSequence_.empty() || !playGate_.empty() || report_.contains("displayOutputSequence") ||
		report_.contains("ambientOcclusionSequence") || report_.contains("shadowSequence") ||
		report_.contains("transformSequence") || report_.contains("frameContextSequence")) {
		throw std::runtime_error("AA比較は他の比較条件やPlayゲートと同時に指定できません");
	}
	auto sequence = JsonFile::Load(path, false);
	const auto valid = [](const nlohmann::json& settings) {
		if (!settings.is_object() || !settings.contains("antialiasing") || !settings["antialiasing"].is_string() ||
			!EnumAdapter<AntialiasingMethod>::FromString(settings["antialiasing"].get<std::string>())) return false;
		for (const char* key : {"shadows", "reflections", "aoEnabled"}) {
			if (settings.contains(key) && !settings[key].is_boolean()) return false;
		}
		for (const char* key : {"aoRadius", "aoIntensity"}) {
			if (settings.contains(key) && (!settings[key].is_number() || !std::isfinite(settings[key].get<float>()) ||
				settings[key].get<float>() < 0.0f)) return false;
		}
		return !settings.contains("frameContextCount") || (settings["frameContextCount"].is_number_unsigned() &&
			(settings["frameContextCount"] == 1 || settings["frameContextCount"] == 2));
	};
	if (!sequence.is_array() || sequence.empty() || sequence.size() > 128 ||
		!std::all_of(sequence.begin(), sequence.end(), valid)) throw std::runtime_error("AA比較の指定が不正です");
	originalAntialiasingPreferences_ = features_->GetPreferences();
	report_["antialiasingSequence"] = std::move(sequence);
	report_["comparison"] = "Antialiasing";
	phaseCount_ = static_cast<uint32_t>(report_["antialiasingSequence"].size());
	report_["phaseCount"] = phaseCount_;
	// 比較の切替をユーザー設定へ保存しない
	features_->SetRuntimeOverrideActive(true);
}

void Engine::EditorRenderBenchmark::BeginAntialiasingPhase(GraphicsCore& graphicsCore) {

	if (!report_.contains("antialiasingSequence")) return;
	const auto& settings = report_["antialiasingSequence"][phase_];
	auto preferences = originalAntialiasingPreferences_;
	preferences.antialiasing = *EnumAdapter<AntialiasingMethod>::FromString(settings.at("antialiasing").get<std::string>());
	preferences.allowInlineRayTracing = settings.value("shadows", preferences.allowInlineRayTracing);
	preferences.reflectionEnabled = settings.value("reflections", preferences.reflectionEnabled);
	preferences.frameContextCount = settings.value("frameContextCount", preferences.frameContextCount);
	preferences.ambientOcclusionEnabled = settings.value("aoEnabled", preferences.ambientOcclusionEnabled);
	preferences.ambientOcclusionRadius = settings.value("aoRadius", preferences.ambientOcclusionRadius);
	preferences.ambientOcclusionIntensity = settings.value("aoIntensity", preferences.ambientOcclusionIntensity);
	features_->ApplyRuntimePreferences(preferences);
	graphicsCore.RequestDoubleBuffering(preferences.frameContextCount == 2);
}

nlohmann::json Engine::EditorRenderBenchmark::CaptureAntialiasingState() const {

	const auto& preferences = features_->GetPreferences();
	nlohmann::json result = {{"requested", report_.at("antialiasingSequence").at(phase_)},
		{"frameContextCount", GraphicsFrameState::GetActiveCount()}, {"views", nlohmann::json::array()}};
	for (auto kind : {RenderViewKind::Game, RenderViewKind::Scene}) {
		const auto& view = application_->renderPipeline_->GetResolvedView(kind);
		result["views"].push_back({{"view", EnumAdapter<RenderViewKind>::ToString(kind)}, {"valid", view.valid},
			{"ambientOcclusionAvailable", application_->renderPipeline_->GetViewAmbientOcclusionTexture(kind) != nullptr},
			{"ambientOcclusionDebug", application_->editorManager_.GetEditorState().gbufferDebugView == GBufferDebugView::AmbientOcclusion},
			{"antialiasing", EnumAdapter<AntialiasingMethod>::ToString(preferences.antialiasing)},
			{"renderWidth", view.width}, {"renderHeight", view.height},
			{"referenceWidth", view.GetReferenceWidth()}, {"referenceHeight", view.GetReferenceHeight()},
			{"outputWidth", view.width}, {"outputHeight", view.height}});
	}
	return result;
}

void Engine::EditorRenderBenchmark::RestoreAntialiasing() {

	if (!features_ || !report_.contains("antialiasingSequence")) return;
	features_->ApplyRuntimePreferences(originalAntialiasingPreferences_);
	features_->SetRuntimeOverrideActive(false);
}
