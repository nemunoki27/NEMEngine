#include "MenuBarPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Panel/Interface/IEditorPanelHost.h>
#include <Engine/Editor/Command/Methods/DeleteEntityCommand.h>
#include <Engine/Core/Graphics/GraphicsPlatform.h>

//============================================================================
//	MenuBarPanel classMethods
//============================================================================

void Engine::MenuBarPanel::Draw(const EditorPanelContext& context) {

	if (!ImGui::BeginMainMenuBar()) {
		return;
	}

	ImGui::SetWindowFontScale(0.72f);

	//============================================================================
	//	シーンファイル操作
	//============================================================================

	if (ImGui::BeginMenu("File")) {

		ImGui::MenuItem("New Scene", nullptr, false, false);
		ImGui::MenuItem("Open Scene...", nullptr, false, false);
		ImGui::MenuItem("Save Scene", "Ctrl+S", false, false);
		ImGui::Separator();
		ImGui::MenuItem("Save Selected As Prefab", nullptr, false, false);
		ImGui::EndMenu();
	}

	//============================================================================
	//	編集操作
	//============================================================================

	if (ImGui::BeginMenu("Edit")) {

		if (!context.editorState) {
			ImGui::EndMenu();
			return;
		}

		// それぞれの操作の実行可能かどうかを判定する
		const bool canUndo = context.editorState && context.editorState->commandHistory.CanUndo();
		const bool canRedo = context.editorState && context.editorState->commandHistory.CanRedo();
		const bool canMutateSelection = context.editorState &&
			context.editorState->HasValidSelection(context.GetWorld()) && context.CanEditScene();
		const bool canPaste = context.editorState && context.editorState->HasClipboard() && context.CanEditScene();

		// 操作を戻す
		if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo)) {
			context.host->UndoEditorCommand();
		}
		// 操作をやり直す
		if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo)) {
			context.host->RedoEditorCommand();
		}

		ImGui::Separator();

		// 選択しているエンティティを複製する
		if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, canMutateSelection)) {
			context.host->DuplicateSelection();
		}
		// 選択しているエンティティをクリップボードにコピーする
		if (ImGui::MenuItem("Copy", "Ctrl+C", false, canMutateSelection)) {
			context.host->CopySelectionToClipboard();
		}
		// クリップボードの内容をシーンに貼り付ける
		if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPaste)) {
			context.host->PasteClipboard();
		}

		ImGui::Separator();

		// 選択しているエンティティを削除する
		if (ImGui::MenuItem("Delete", "Del", false, canMutateSelection)) {
			context.host->ExecuteEditorCommand(std::make_unique<DeleteEntityCommand>(context.editorState->selectedEntity));
		}
		ImGui::EndMenu();
	}

	//============================================================================
	//	エディタウィンドウ表示設定
	//============================================================================

	if (ImGui::BeginMenu("Window")) {

		ImGui::MenuItem("Toolbar", nullptr, &context.layoutState->showToolbar);
		ImGui::MenuItem("Hierarchy", nullptr, &context.layoutState->showHierarchy);
		ImGui::MenuItem("Inspector", nullptr, &context.layoutState->showInspector);
		ImGui::MenuItem("Project", nullptr, &context.layoutState->showProject);
		ImGui::MenuItem("Console", nullptr, &context.layoutState->showConsole);
		ImGui::MenuItem("Tool", nullptr, &context.layoutState->showTool);
		ImGui::MenuItem("SceneView", nullptr, &context.layoutState->showSceneView);
		ImGui::MenuItem("GameView", nullptr, &context.layoutState->showGameView);

		ImGui::EndMenu();
	}
	//============================================================================
	//	グラフィックス機能表示/切り替え
	//============================================================================

	if (ImGui::BeginMenu("Graphics")) {

		// GPUから検出した機能サポート状況とユーザー設定を表示し、切り替え可能なものは切り替える
		auto& featureController = context.graphicsPlatform->GetFeatureController();
		const auto& adapterInfo = featureController.GetAdapterInfo();
		const auto& support = featureController.GetSupport();
		const auto& preferences = featureController.GetPreferences();
		const auto& runtime = featureController.GetRuntimeFeatures();
		const double vramGB = static_cast<double>(adapterInfo.dedicatedVideoMemoryBytes) / (1024.0 * 1024.0 * 1024.0);

		ImGui::TextWrapped("Adapter: %s", adapterInfo.adapterName.empty() ? "Unknown" : adapterInfo.adapterName.c_str());
		ImGui::Text("Feature Level : %s", GraphicsFeatureText::ToString(adapterInfo.featureLevel));
		ImGui::Text("Shader Model  : %s", GraphicsFeatureText::ToString(support.highestShaderModel));
		ImGui::Text("Dedicated VRAM: %.2f GB", vramGB);

		ImGui::Separator();

		ImGui::Text("Mesh Shader Tier: %s", GraphicsFeatureText::ToString(support.meshShaderTier));
		ImGui::Text("RayTracing Tier : %s", GraphicsFeatureText::ToString(support.raytracingTier));
		ImGui::Text("Wave Ops        : %s", support.waveOps ? "Supported" : "Not Supported");

		ImGui::Separator();

		bool allowMeshShader = preferences.allowMeshShader;
		ImGui::BeginDisabled(!support.SupportsMeshShaderPath());
		if (ImGui::Checkbox("Use Mesh Shader", &allowMeshShader)) {
			featureController.SetAllowMeshShader(allowMeshShader);
		}
		ImGui::EndDisabled();

		if (!support.SupportsMeshShaderPath()) {
			ImGui::TextDisabled("Mesh Shader path is unavailable on this GPU.");
		}
		ImGui::Text("Current Mesh Path: %s", runtime.useMeshShader ? "Mesh Shader" : "Legacy Raster Fallback");

		ImGui::Separator();

		bool allowInlineRayTracing = preferences.allowInlineRayTracing;
		ImGui::BeginDisabled(!support.SupportsRayTracingPath());
		if (ImGui::Checkbox("Use Inline RayTracing", &allowInlineRayTracing)) {
			featureController.SetAllowInlineRayTracing(allowInlineRayTracing);
		}
		ImGui::EndDisabled();

		if (!support.SupportsRayTracingPath()) {
			ImGui::TextDisabled("Inline RayTracing path is unavailable on this GPU.");
		}

		bool allowDispatchRays = preferences.allowDispatchRays;
		ImGui::BeginDisabled(!support.SupportsRayTracingPath());
		if (ImGui::Checkbox("Use DispatchRays (DXR)", &allowDispatchRays)) {
			featureController.SetAllowDispatchRays(allowDispatchRays);
		}
		ImGui::EndDisabled();

		if (!support.SupportsRayTracingPath()) {
			ImGui::TextDisabled("DispatchRays path is unavailable on this GPU.");
		}

		ImGui::Text("Inline RayTracing : %s", runtime.useInlineRayTracing ? "Enabled" : "Disabled");
		ImGui::Text("DispatchRays      : %s", runtime.useDispatchRays ? "Enabled" : "Disabled");
		ImGui::Text("Ray Scene Build   : %s", runtime.UsesAnyRayTracing() ? "Enabled" : "Disabled");

		ImGui::EndMenu();
	}
	ImGui::SetWindowFontScale(1.0f);

	ImGui::EndMainMenuBar();
}