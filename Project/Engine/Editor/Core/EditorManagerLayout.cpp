#include "EditorManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Views/ViewportRenderService.h>
#include <Engine/Editor/UI/Panels/Builtin/BuiltinEditorPanelRegistration.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <filesystem>
#include <optional>

namespace {

	constexpr const char* kViewportPanelStateConfigPath = Engine::ConfigPaths::kViewportPanel;

	template <typename Enum>
	void LoadEnumValue(const nlohmann::json& data, const char* key, Enum& value) {

		if (!data.contains(key) || !data[key].is_string()) {
			return;
		}

		if (std::optional<Enum> loaded = Engine::EnumAdapter<Enum>::FromString(
			data[key].get<std::string>())) {
			value = loaded.value();
		}
	}
}

//============================================================================
//	EditorManager layout methods
//============================================================================
void Engine::EditorManager::RequestDuplicatePanel(const std::string& instanceID) {

	pendingDuplicatePanelID_ = instanceID;
}

const std::vector<Engine::EditorLayoutMenuEntry>& Engine::EditorManager::GetEditorLayoutEntries() const {

	return editorLayoutManager_.GetMenuEntries();
}

const std::string& Engine::EditorManager::GetActiveEditorLayoutID() const {

	return editorLayoutManager_.GetActiveLayoutID();
}

bool Engine::EditorManager::IsEngineLayoutSaveAvailable() const {

	return editorLayoutManager_.IsEngineSourceProject();
}

bool Engine::EditorManager::RequestSaveEditorLayout(const std::string& name, std::string& outError) {

	EditorLayoutSnapshot layout = CaptureEditorLayout();
	std::string layoutID;
	if (!editorLayoutManager_.SaveUserLayout(name, layout, layoutID, outError)) {
		return false;
	}

	if (const EditorLayoutSnapshot* savedLayout = editorLayoutManager_.FindLayout(layoutID)) {
		editorLayoutManager_.SaveSession(*savedLayout);
	}
	return true;
}

void Engine::EditorManager::RequestSaveAllEngineLayouts() {

	std::string error;
	if (!editorLayoutManager_.SaveAllEngineLayouts(error)) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"Editor layout export failed: {}", error);
	}
}

void Engine::EditorManager::RequestApplyEditorLayout(const std::string& layoutID) {

	const EditorLayoutSnapshot* layout = editorLayoutManager_.FindLayout(layoutID);
	if (!layout) {
		return;
	}
	pendingEditorLayout_ = *layout;
	editorLayoutManager_.SetActiveLayoutID(layoutID);
}

void Engine::EditorManager::RequestDeleteEditorLayout(const std::string& layoutID) {

	const bool deletingActive = editorLayoutManager_.GetActiveLayoutID() == layoutID;
	if (!editorLayoutManager_.DeleteLayout(layoutID) || !deletingActive) {
		return;
	}

	if (const EditorLayoutSnapshot* defaultLayout = editorLayoutManager_.FindLayout(
		editorLayoutManager_.GetActiveLayoutID())) {
		pendingEditorLayout_ = *defaultLayout;
	}
}

void Engine::EditorManager::RequestImportEditorLayouts() {

	EditorLayoutSnapshot defaultLayout{};
	std::string error;
	if (editorLayoutManager_.ImportEngineLayouts(defaultLayout, error)) {
		pendingEditorLayout_ = std::move(defaultLayout);
	}
}

void Engine::EditorManager::DrawPanelsByPhase(const EditorPanelContext& context, EditorPanelPhase phase) {

	if (context.layoutState && context.layoutState->hidePanels) {
		return;
	}

	for (const auto& panel : panels_) {
		if (panel->GetPhase() != phase) {
			continue;
		}
		panel->Draw(context);
	}
}

Engine::EditorLayoutSnapshot Engine::EditorManager::CaptureEditorLayout() const {

	EditorLayoutSnapshot layout{};
	layout.layoutID = editorLayoutManager_.GetActiveLayoutID();
	if (const EditorLayoutSnapshot* active = editorLayoutManager_.FindLayout(layout.layoutID)) {
		layout.displayName = active->displayName;
	}
	if (layout.displayName.empty()) {
		layout.displayName = "Current";
	}

	layout.visibility.showHierarchy = layoutState_.showHierarchy;
	layout.visibility.showInspector = layoutState_.showInspector;
	layout.visibility.showProject = layoutState_.showProject;
	layout.visibility.showConsole = layoutState_.showConsole;
	layout.visibility.showSceneView = layoutState_.showSceneView;
	layout.visibility.showGameView = layoutState_.showGameView;
	layout.visibility.showToolbar = layoutState_.showToolbar;
	layout.visibility.showTool = layoutState_.showTool;

	for (const auto& panel : panels_) {

		if (panel->GetPanelTypeID() != "Project" && panel->GetPanelTypeID() != "Inspector") {
			continue;
		}

		bool open = panel->IsInstanceOpen();
		if (panel->IsPrimaryInstance()) {
			open = panel->GetPanelTypeID() == "Project" ?
				layoutState_.showProject : layoutState_.showInspector;
		}
		layout.panels.push_back({
			.typeID = panel->GetPanelTypeID(),
			.instanceID = panel->GetInstanceID(),
			.primary = panel->IsPrimaryInstance(),
			.open = open,
			.state = panel->SaveLayoutState(),
			});
	}

	size_t iniSize = 0;
	const char* iniData = ImGui::SaveIniSettingsToMemory(&iniSize);
	if (iniData && iniSize != 0) {
		layout.imguiIniData.assign(iniData, iniSize);
	}
	return layout;
}

void Engine::EditorManager::ApplyEditorLayout(const EditorLayoutSnapshot& layout, GraphicsCore& graphicsCore) {

	layoutState_.showHierarchy = layout.visibility.showHierarchy;
	layoutState_.showInspector = layout.visibility.showInspector;
	layoutState_.showProject = layout.visibility.showProject;
	layoutState_.showConsole = layout.visibility.showConsole;
	layoutState_.showSceneView = layout.visibility.showSceneView;
	layoutState_.showGameView = layout.visibility.showGameView;
	layoutState_.showToolbar = layout.visibility.showToolbar;
	layoutState_.showTool = layout.visibility.showTool;

	// 適用前の複製パネルを破棄しスナップショットから作り直す
	panels_.erase(std::remove_if(panels_.begin(), panels_.end(), [](const std::unique_ptr<IEditorPanel>& panel) {
		return !panel->IsPrimaryInstance() && !panel->GetPanelTypeID().empty();
		}), panels_.end());

	EditorPanelCreateContext createContext{ graphicsCore.GetTextureUploadService() };
	for (const EditorPanelLayoutSnapshot& panelLayout : layout.panels) {

		if (panelLayout.typeID != "Project" && panelLayout.typeID != "Inspector") {
			continue;
		}

		if (panelLayout.primary) {

			auto found = std::find_if(panels_.begin(), panels_.end(), [&](const std::unique_ptr<IEditorPanel>& panel) {
				return panel->IsPrimaryInstance() && panel->GetPanelTypeID() == panelLayout.typeID;
				});
			if (found != panels_.end()) {
				(*found)->LoadLayoutState(panelLayout.state);
			}
			continue;
		}

		std::unique_ptr<IEditorPanel> panel = CreateBuiltinEditorPanelInstance(
			createContext, panelLayout.typeID, panelLayout.instanceID);
		if (!panel) {
			continue;
		}
		panel->SetInstanceOpen(panelLayout.open);
		panel->LoadLayoutState(panelLayout.state);
		panels_.emplace_back(std::move(panel));
	}

	ImGui::ClearIniSettings();
	if (!layout.imguiIniData.empty()) {
		ImGui::LoadIniSettingsFromMemory(layout.imguiIniData.c_str(), layout.imguiIniData.size());
		requestBuildDefaultDockLayout_ = false;
	} else {
		requestBuildDefaultDockLayout_ = layout.builtinDefault;
	}
}

void Engine::EditorManager::ApplyPendingEditorLayout(GraphicsCore& graphicsCore) {

	if (!pendingEditorLayout_) {
		return;
	}
	ApplyEditorLayout(pendingEditorLayout_.value(), graphicsCore);
	editorLayoutManager_.SaveSession(pendingEditorLayout_.value());
	pendingEditorLayout_.reset();
}

void Engine::EditorManager::ApplyPendingPanelDuplicate(const EditorPanelContext& context) {

	if (pendingDuplicatePanelID_.empty()) {
		return;
	}

	IEditorPanel* source = FindPanelByInstanceID(pendingDuplicatePanelID_);
	pendingDuplicatePanelID_.clear();
	if (!source || !source->CanDuplicate(context)) {
		return;
	}

	const std::string typeID = source->GetPanelTypeID();
	int32_t panelCount = 0;
	for (const auto& panel : panels_) {
		if (panel->GetPanelTypeID() == typeID) {
			++panelCount;
		}
	}

	const std::string instanceID = typeID + "." + ToString(UUID::New());
	const std::string displayName = typeID == "Project" ?
		"Project " + std::to_string(panelCount + 1) : std::string{};
	EditorPanelCreateContext createContext{ context.graphicsCore->GetTextureUploadService() };
	std::unique_ptr<IEditorPanel> duplicated = CreateBuiltinEditorPanelInstance(
		createContext, typeID, instanceID, displayName);
	if (!duplicated) {
		return;
	}

	duplicated->LoadLayoutState(source->MakeDuplicateState(context));
	duplicated->SetInitialDockID(source->GetCurrentDockID());
	panels_.emplace_back(std::move(duplicated));
	ImGui::GetIO().WantSaveIniSettings = true;
}

void Engine::EditorManager::RemoveClosedDuplicatedPanels() {

	panels_.erase(std::remove_if(panels_.begin(), panels_.end(), [](const std::unique_ptr<IEditorPanel>& panel) {
		return !panel->IsPrimaryInstance() && !panel->IsInstanceOpen();
		}), panels_.end());
}

Engine::IEditorPanel* Engine::EditorManager::FindPanelByInstanceID(const std::string& instanceID) const {

	const auto found = std::find_if(panels_.begin(), panels_.end(), [&](const std::unique_ptr<IEditorPanel>& panel) {
		return panel->GetInstanceID() == instanceID;
		});
	return found != panels_.end() ? found->get() : nullptr;
}

void Engine::EditorManager::UpdateSceneViewManualCamera() {

	// ビューが非表示なら更新しない
	// カメラモードがマニュアルでないなら更新しない
	// シーンギズモを使用している場合は更新しない
	if (!layoutState_.showSceneView ||
		editorState_.sceneViewCamera.mode != SceneViewCameraMode::DebugManual ||
		editorState_.useSceneGizmo) {
		return;
	}

	// ツールウィンドウ等がシーンビューの上に重なっている場合はカメラを更新しない
	// HoveredWindowが前フレームのマウス下ウィンドウを示すため、SceneView以外はスキップする
	ImGuiContext* ctx = ImGui::GetCurrentContext();
	if (ctx && ctx->HoveredWindow) {
		const char* name = ctx->HoveredWindow->Name;
		if (name && std::strstr(name, "SceneView") == nullptr) {
			return;
		}
	}

	// カメラの状態を更新する
	sceneViewCameraController_->Update(editorState_.manualCameraDimension, InputViewArea::Scene);
}

void Engine::EditorManager::LoadViewportPanelState() {

	const std::filesystem::path configPath = RuntimePaths::GetGameConfigPath(kViewportPanelStateConfigPath);
	if (!JsonAdapter::Check(configPath.string(), false)) {
		return;
	}

	const nlohmann::json data = JsonAdapter::Load(configPath.string(), false);
	if (!data.is_object() || !data.contains("sceneView") || !data["sceneView"].is_object()) {
		return;
	}

	const nlohmann::json& sceneView = data["sceneView"];
	if (sceneView.contains("drawDefaultGrid") && sceneView["drawDefaultGrid"].is_boolean()) {
		editorState_.drawSceneViewDefaultGrid = sceneView["drawDefaultGrid"].get<bool>();
	}
	if (sceneView.contains("enableSnapEditEntity") && sceneView["enableSnapEditEntity"].is_boolean()) {
		editorState_.enableSnapEditEntity = sceneView["enableSnapEditEntity"].get<bool>();
	}
	// ギズモのスナップ設定を読み込む
	if (sceneView.contains("snap") && sceneView["snap"].is_object()) {

		const nlohmann::json& snap = sceneView["snap"];
		auto loadAxis = [&snap](const char* key, GridSnapAxis& axis) {
			if (!snap.contains(key) || !snap[key].is_object()) {
				return;
			}
			const nlohmann::json& a = snap[key];
			if (a.contains("size") && a["size"].is_number()) {
				axis.size = a["size"].get<float>();
			}
			if (a.contains("absolute") && a["absolute"].is_boolean()) {
				axis.absolute = a["absolute"].get<bool>();
			}
			};
		EntitySnapSettings& s = editorState_.snapSettings;
		loadAxis("translate2D", s.translate2D);
		loadAxis("rotate2D", s.rotate2D);
		loadAxis("scale2D", s.scale2D);
		loadAxis("translate3D", s.translate3D);
		loadAxis("rotate3D", s.rotate3D);
		loadAxis("scale3D", s.scale3D);
		if (snap.contains("drawGrid") && snap["drawGrid"].is_boolean()) {
			s.drawSnapGrid = snap["drawGrid"].get<bool>();
		}
	}

	LoadEnumValue(sceneView, "manipulatorMode", editorState_.sceneViewManipulatorMode);
	LoadEnumValue(sceneView, "cameraMode", editorState_.sceneViewCamera.mode);
	LoadEnumValue(sceneView, "manualCameraDimension", editorState_.manualCameraDimension);

	// 実体参照は起動時に持ち越さずモードだけを復元しカメラ指定は現在のシーンで選び直す
	editorState_.sceneViewCamera.ClearAssignedCameras();
	editorState_.ClearSelection();
}

void Engine::EditorManager::SaveViewportPanelState() const {

	nlohmann::json sceneView = nlohmann::json::object();
	sceneView["drawDefaultGrid"] = editorState_.drawSceneViewDefaultGrid;
	sceneView["enableSnapEditEntity"] = editorState_.enableSnapEditEntity;
	// ギズモのスナップ設定を書き出す
	{
		auto saveAxis = [](const GridSnapAxis& axis) {
			return nlohmann::json{ { "size", axis.size }, { "absolute", axis.absolute } };
			};
		const EntitySnapSettings& s = editorState_.snapSettings;
		nlohmann::json snap = nlohmann::json::object();
		snap["translate2D"] = saveAxis(s.translate2D);
		snap["rotate2D"] = saveAxis(s.rotate2D);
		snap["scale2D"] = saveAxis(s.scale2D);
		snap["translate3D"] = saveAxis(s.translate3D);
		snap["rotate3D"] = saveAxis(s.rotate3D);
		snap["scale3D"] = saveAxis(s.scale3D);
		snap["drawGrid"] = s.drawSnapGrid;
		sceneView["snap"] = snap;
	}
	sceneView["manipulatorMode"] = EnumAdapter<SceneViewManipulatorMode>::ToString(editorState_.sceneViewManipulatorMode);
	sceneView["cameraMode"] = EnumAdapter<SceneViewCameraMode>::ToString(editorState_.sceneViewCamera.mode);
	sceneView["manualCameraDimension"] = EnumAdapter<Dimension>::ToString(editorState_.manualCameraDimension);

	nlohmann::json data = nlohmann::json::object();
	data["sceneView"] = sceneView;

	const std::filesystem::path configPath = RuntimePaths::GetGameConfigPath(kViewportPanelStateConfigPath);
	JsonAdapter::Save(configPath.string(), data);
}

