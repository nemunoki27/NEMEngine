#include "MenuBarPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Commands/Entity/DeleteEntityCommand.h>
#include <Engine/Core/Rendering/Core/RenderingPlatform.h>
#include <Engine/Core/Foundation/Time/FrameRateSettings.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <string>

namespace {

	// エンジンDLLのビルド時刻を "Mmm dd hh:mm" で返す、SDK(エンジン)を作り直すと更新される
	const char* GetEngineBuildVersion() {

		static const std::string version = []() {
			const std::string date = __DATE__; // "Mmm dd yyyy"
			const std::string time = __TIME__; // "hh:mm:ss"
			std::string day = date.substr(4, 2);
			// 1桁の日はスペース埋めされるので0埋めへ直す
			if (!day.empty() && day[0] == ' ') { day[0] = '0'; }
			return date.substr(0, 3) + " " + day + " " + time.substr(0, 5);
			}();
		return version.c_str();
	}
}

//============================================================================
//	MenuBarPanel classMethods
//============================================================================
void Engine::MenuBarPanel::Draw(const EditorPanelContext& context) {

	gameBuildService_.Update();
	if (!ImGui::BeginMainMenuBar()) {
		return;
	}

	ImGui::SetWindowFontScale(0.85f);

	//============================================================================
	//	製品ビルド
	//============================================================================
	DrawGameBuildMenu(context);

	//============================================================================
	//	シーンファイル操作
	//============================================================================
	if (ImGui::BeginMenu("ファイル")) {

		ImGui::SetWindowFontScale(0.72f);

		const bool canEditScene = context.CanEditScene();
		const bool isPrefabEditing = context.editorContext && context.editorContext->isPrefabEditing;
		if (ImGui::MenuItem("シーン作成", nullptr, false, canEditScene && !isPrefabEditing)) {
			context.host->RequestNewScene();
		}
		// プレファブ編集中は同じ項目で隔離ワールドを.prefabへ保存する
		if (ImGui::MenuItem(isPrefabEditing ? "プレファブを保存" : "シーンを保存", "Ctrl+S", false, canEditScene)) {
			if (isPrefabEditing) {
				context.host->RequestSavePrefab();
			} else {
				context.host->RequestSaveScene();
			}
		}

		ImGui::SetWindowFontScale(1.0f);

		ImGui::EndMenu();
	}

	//============================================================================
	//	編集操作
	//============================================================================
	if (ImGui::BeginMenu("編集補助")) {

		ImGui::SetWindowFontScale(0.72f);

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
		if (ImGui::MenuItem("複製", "Ctrl+D", false, canMutateSelection)) {
			context.host->DuplicateSelection();
		}
		// 選択しているエンティティをクリップボードにコピーする
		if (ImGui::MenuItem("コピー", "Ctrl+C", false, canMutateSelection)) {
			context.host->CopySelectionToClipboard();
		}
		// クリップボードの内容をシーンに貼り付ける
		if (ImGui::MenuItem("コピー済みをペースト", "Ctrl+V", false, canPaste)) {
			context.host->PasteClipboard();
		}

		ImGui::Separator();

		// 選択しているエンティティを削除する
		if (ImGui::MenuItem("削除", "Del", false, canMutateSelection)) {
			context.host->ExecuteEditorCommand(std::make_unique<DeleteEntityCommand>(context.editorState->selectedEntity));
		}
		ImGui::Separator();

		// 複数選択
		if (ImGui::MenuItem("複数選択", "Shift+左クリック", false, canMutateSelection)) {
			context.host->ExecuteEditorCommand(std::make_unique<DeleteEntityCommand>(context.editorState->selectedEntity));
		}

		ImGui::SetWindowFontScale(1.0f);

		ImGui::EndMenu();
	}

	//============================================================================
	//	エディタウィンドウ表示設定
	//============================================================================
	if (ImGui::BeginMenu("ウィンドウ")) {

		ImGui::SetWindowFontScale(0.72f);

		ImGui::MenuItem("パネルを全て非表示", "Tab+Esc", &context.layoutState->hidePanels);
		ImGui::Separator();

		ImGui::MenuItem("Toolbar", nullptr, &context.layoutState->showToolbar);
		ImGui::MenuItem("Hierarchy", nullptr, &context.layoutState->showHierarchy);
		ImGui::MenuItem("Inspector", nullptr, &context.layoutState->showInspector);
		ImGui::MenuItem("Project", nullptr, &context.layoutState->showProject);
		ImGui::MenuItem("Console", nullptr, &context.layoutState->showConsole);
		ImGui::MenuItem("Tool", nullptr, &context.layoutState->showTool);
		ImGui::MenuItem("SceneView", nullptr, &context.layoutState->showSceneView);
		ImGui::MenuItem("GameView", nullptr, &context.layoutState->showGameView);

		ImGui::SetWindowFontScale(1.0f);

		ImGui::EndMenu();
	}

	//============================================================================
	//	グラフィックス機能表示/切り替え
	//============================================================================
	if (ImGui::BeginMenu("グラフィックス設定")) {

		ImGui::SetWindowFontScale(0.85f);

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

		ImGui::Separator();

		// メッシュ描画経路はGPU対応状況を見ながら切り替える
		bool allowMeshShader = preferences.allowMeshShader;
		ImGui::BeginDisabled(!support.SupportsMeshShaderPath());
		if (ImGui::Checkbox("メッシュシェーダーを使用", &allowMeshShader)) {
			featureController.SetAllowMeshShader(allowMeshShader);
		}
		ImGui::EndDisabled();

		if (!support.SupportsMeshShaderPath()) {
			ImGui::TextDisabled("メッシュシェーダーに対応していないGPUです");
		}
		ImGui::Text("現在のメッシュパス: %s", runtime.useMeshShader ? "メッシュシェーダー" : "頂点シェーダ―");

		ImGui::Separator();

		// フレームレート上限はここで切り替えてProjectSettingsへ保存する、0は制限なし
		FrameRateSettings& frameRate = FrameRateSettings::GetInstance();
		const uint32_t fpsOptions[] = { 30u, 60u, 120u, 0u };
		const char* fpsLabels[] = { "30", "60", "120", "未制限" };
		int fpsIndex = 1;
		for (int i = 0; i < 4; ++i) {
			if (fpsOptions[i] == frameRate.GetTargetFps()) {
				fpsIndex = i;
				break;
			}
		}
		if (ImGui::Combo("フレームレート制限", &fpsIndex, fpsLabels, 4)) {
			frameRate.SetTargetFps(fpsOptions[fpsIndex]);
			frameRate.Save();
		}

		ImGui::Separator();

		// カリング系はGameView基準の結果を確認しやすいよう、Graphicsメニューから個別に切り替える
		bool allowFrustumCulling = preferences.allowFrustumCulling;
		if (ImGui::Checkbox("視錐台カリング有効", &allowFrustumCulling)) {
			featureController.SetAllowFrustumCulling(allowFrustumCulling);
		}
		ImGui::Text("視錐台カリング: %s", runtime.useFrustumCulling ? "有効" : "無効");

		ImGui::Separator();

		bool allowInlineRayTracing = preferences.allowInlineRayTracing;
		ImGui::BeginDisabled(!support.SupportsRayTracingPath());
		if (ImGui::Checkbox("インラインシャドウ有効", &allowInlineRayTracing)) {
			featureController.SetAllowInlineRayTracing(allowInlineRayTracing);
		}
		ImGui::EndDisabled();

		if (!support.SupportsRayTracingPath()) {
			ImGui::TextDisabled("インラインレイトレーシングに対応していないGPUです");
		}

		bool allowDispatchRays = preferences.allowDispatchRays;
		ImGui::BeginDisabled(!support.SupportsRayTracingPath());
		if (ImGui::Checkbox("マテリアル反射パス有効", &allowDispatchRays)) {
			featureController.SetAllowDispatchRays(allowDispatchRays);
		}
		ImGui::EndDisabled();

		if (!support.SupportsRayTracingPath()) {
			ImGui::TextDisabled("レイトレーシングに対応していないGPUです");
		}

		ImGui::Text("インラインシャドウ : %s", runtime.useInlineRayTracing ? "有効" : "無効");
		ImGui::Text("マテリアル反射パス : %s", runtime.useDispatchRays ? "有効" : "無効");
		ImGui::Text("TLAS ビルド      : %s", runtime.UsesAnyRayTracing() ? "有効" : "無効");

		ImGui::Separator();

		// DeferredのGBufferをGameView/SceneViewへ表示する、チェックは常に1つだけ、全部外すと通常描画へ戻る
		ImGui::TextDisabled("Deferred GBuffer View");
		if (context.editorState) {

			struct GBufferDebugItem {

				const char* label;
				GBufferDebugView view;
			};
			static const GBufferDebugItem kItems[] = {
				{ "Albedo", GBufferDebugView::Albedo },
				{ "Normal", GBufferDebugView::Normal },
				{ "World Pos",    GBufferDebugView::Position },
				{ "Material", GBufferDebugView::Material },
				{ "Emissive", GBufferDebugView::Emissive },
				{ "Depth", GBufferDebugView::Depth },
			};

			GBufferDebugView& current = context.editorState->gbufferDebugView;
			for (const GBufferDebugItem& item : kItems) {

				bool checked = (current == item.view);
				if (ImGui::Checkbox(item.label, &checked)) {
					// 1つだけ選べるようにし、同じ項目を外したらNoneへ戻す
					current = checked ? item.view : GBufferDebugView::None;
				}
			}
			ImGui::Text("現在の表示: %s", EnumAdapter<GBufferDebugView>::ToString(current));
		}

		ImGui::SetWindowFontScale(1.0f);

		ImGui::EndMenu();
	}

	//============================================================================
	//	エディターレイアウト設定
	//============================================================================
	DrawEditorLayoutMenu(context);

	// 一番右にエンジンのビルド時刻をバージョンとして表示する
	ImGui::TextDisabled("エンジンのバージョン: %s", GetEngineBuildVersion());

	ImGui::SetWindowFontScale(1.0f);

	ImGui::EndMainMenuBar();
	DrawGameBuildPopup(context);
	DrawLayoutSavePopup(context);
}

void Engine::MenuBarPanel::DrawGameBuildMenu(const EditorPanelContext& context) {

	if (!ImGui::BeginMenu("ビルド")) {
		return;
	}

	ImGui::SetWindowFontScale(0.72f);
	const bool canBuild = context.editorContext && context.editorContext->assetDatabase &&
		!context.IsPlaying() && !gameBuildService_.IsBuilding();
	if (ImGui::MenuItem("ビルド", nullptr, false, canBuild)) {

		PrepareGameBuildPopup(context);
		requestOpenBuildPopup_ = true;
	}
	ImGui::SetWindowFontScale(1.0f);
	ImGui::EndMenu();
}

void Engine::MenuBarPanel::DrawGameBuildPopup(const EditorPanelContext& context) {

	constexpr const char* popupName = "ゲームのビルド";
	std::optional<std::filesystem::path> selectedDirectory;
	if (buildDirectoryDialog_.Poll(selectedDirectory) && selectedDirectory) {
		buildOutputPath_ = Algorithm::PathToUTF8(*selectedDirectory);
	}
	if (requestOpenBuildPopup_) {

		ImGui::OpenPopup(popupName);
		requestOpenBuildPopup_ = false;
	}

	ImGui::SetNextWindowSizeConstraints(ImVec2(1000.0f, 0.0f), ImVec2(1000.0f, FLT_MAX));
	if (!ImGui::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	const bool isBuilding = gameBuildService_.IsBuilding();
	ImGui::BeginDisabled(isBuilding);
	{
		MyGUI::ScopedPropertyLabelWidth labelWidth("GameBuildSettings");
		MyGUI::StringCombo("最初のシーン", buildSceneName_, buildSceneNames_, "<シーンがありません>");
		MyGUI::InputText("Exeの名前", buildExecutableName_);

		if (MyGUI::BeginPropertyRow("出力先")) {

			const float buttonWidth = ImGui::CalcTextSize("参照").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			const float inputWidth = (std::max)(80.0f,
				ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
			ImGui::SetNextItemWidth(inputWidth);
			ImGui::InputText("##GameBuildOutputPath", &buildOutputPath_, ImGuiInputTextFlags_ReadOnly);
			ImGui::SameLine();
			ImGui::BeginDisabled(buildDirectoryDialog_.IsOpen());
			if (ImGui::Button("参照")) {
				buildDirectoryDialog_.Open(Algorithm::PathFromUTF8(buildOutputPath_));
			}
			ImGui::EndDisabled();
			MyGUI::EndPropertyRow();
		}
		MyGUI::Checkbox("起動時にフルスクリーン", buildStartupFullscreen_);
	}
	ImGui::EndDisabled();

	ImGui::Separator();
	const GameBuildState state = gameBuildService_.GetState();
	if (state == GameBuildState::Building) {
		ImGui::TextDisabled("ビルド中...");
	} else if (state == GameBuildState::Completed) {
		ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "完了しました");
		const std::string outputDirectory =
			Algorithm::PathToUTF8(gameBuildService_.GetOutputDirectory());
		ImGui::TextWrapped("%s", outputDirectory.c_str());
	} else if (state == GameBuildState::Failed) {
		ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "失敗しました");
		if (!gameBuildService_.GetFailureDetail().empty()) {
			ImGui::TextWrapped("%s", gameBuildService_.GetFailureDetail().c_str());
		}
	} else if (!buildError_.empty()) {
		ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", buildError_.c_str());
	}

	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const float buttonWidth = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
	ImGui::BeginDisabled(isBuilding || buildSceneNames_.empty());
	if (ImGui::Button("ビルド", ImVec2(buttonWidth, 0.0f))) {

		buildError_.clear();
		GameBuildSettings settings{};
		settings.startupScene = ResolveBuildScene();
		settings.executableName = buildExecutableName_;
		settings.outputRoot = Algorithm::PathFromUTF8(buildOutputPath_);
		settings.startupFullscreen = buildStartupFullscreen_;
		if (!context.editorContext || !context.editorContext->assetDatabase ||
			!gameBuildService_.Start(settings, *context.editorContext->assetDatabase, buildError_)) {

			if (buildError_.empty()) {
				buildError_ = "製品ビルドを開始できませんでした";
			}
		}
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(isBuilding);
	if (ImGui::Button("キャンセル", ImVec2(buttonWidth, 0.0f))) {

		buildError_.clear();
		gameBuildService_.ResetStatus();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();

	ImGui::EndPopup();
}

void Engine::MenuBarPanel::PrepareGameBuildPopup(const EditorPanelContext& context) {

	buildError_.clear();
	gameBuildService_.ResetStatus();
	gameBuildService_.RefreshScenes(*context.editorContext->assetDatabase);

	buildSceneNames_.clear();
	buildSceneNames_.reserve(gameBuildService_.GetScenes().size());
	for (const GameBuildSceneEntry& scene : gameBuildService_.GetScenes()) {
		buildSceneNames_.push_back(scene.displayName);
	}

	const AssetID activeScene = context.editorContext->activeSceneAsset;
	const auto active = std::find_if(gameBuildService_.GetScenes().begin(), gameBuildService_.GetScenes().end(),
		[activeScene](const GameBuildSceneEntry& scene) { return scene.assetID == activeScene; });
	if (active != gameBuildService_.GetScenes().end()) {
		buildSceneName_ = active->displayName;
	} else if (!buildSceneNames_.empty()) {
		buildSceneName_ = buildSceneNames_.front();
	} else {
		buildSceneName_.clear();
	}

	if (buildExecutableName_.empty()) {
		buildExecutableName_ = Algorithm::PathToUTF8(RuntimePaths::GetGameRoot().filename());
	}
	if (buildOutputPath_.empty()) {
		buildOutputPath_ = Algorithm::PathToUTF8(
			RuntimePaths::GetEngineProjectRoot().parent_path() / "Build");
	}
}

Engine::AssetID Engine::MenuBarPanel::ResolveBuildScene() const {

	const auto found = std::find_if(gameBuildService_.GetScenes().begin(), gameBuildService_.GetScenes().end(),
		[this](const GameBuildSceneEntry& scene) { return scene.displayName == buildSceneName_; });
	return found != gameBuildService_.GetScenes().end() ? found->assetID : AssetID{};
}

void Engine::MenuBarPanel::DrawEditorLayoutMenu(const EditorPanelContext& context) {

	if (!ImGui::BeginMenu("エディターレイアウト設定")) {
		return;
	}

	ImGui::SetWindowFontScale(0.72f);
	if (ImGui::MenuItem("現在のレイアウトを保存")) {

		layoutNameBuffer_.clear();
		layoutSaveError_.clear();
		requestOpenLayoutSavePopup_ = true;
	}
	if (!context.host->IsEngineLayoutSaveAvailable() && ImGui::MenuItem("レイアウトインポート")) {
		context.host->RequestImportEditorLayouts();
	}
	if (context.host->IsEngineLayoutSaveAvailable() &&
		ImGui::MenuItem("エンジン共有レイアウトを保存")) {
		context.host->RequestSaveAllEngineLayouts();
	}

	ImGui::Separator();
	const std::string activeLayoutID = context.host->GetActiveEditorLayoutID();
	std::string deleteLayoutID;
	for (const EditorLayoutMenuEntry& entry : context.host->GetEditorLayoutEntries()) {

		ImGui::PushID(entry.layoutID.c_str());
		const bool selected = activeLayoutID == entry.layoutID;
		if (ImGui::MenuItem(entry.displayName.c_str(), nullptr, selected)) {
			context.host->RequestApplyEditorLayout(entry.layoutID);
		}

		if (!entry.defaultLayout && ImGui::BeginPopupContextItem("##LayoutContext", ImGuiPopupFlags_MouseButtonRight)) {

			if (ImGui::MenuItem("削除")) {
				deleteLayoutID = entry.layoutID;
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
	if (!deleteLayoutID.empty()) {
		context.host->RequestDeleteEditorLayout(deleteLayoutID);
	}

	ImGui::SetWindowFontScale(1.0f);
	ImGui::EndMenu();
}

void Engine::MenuBarPanel::DrawLayoutSavePopup(const EditorPanelContext& context) {

	constexpr const char* popupName = "エディターレイアウトの保存";
	if (requestOpenLayoutSavePopup_) {

		ImGui::OpenPopup(popupName);
		requestOpenLayoutSavePopup_ = false;
	}
	if (!ImGui::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::Text("レイアウト名");
	ImGui::Separator();
	TextInputPopupResult inputResult = MyGUI::InputTextPopupContent("名前", layoutNameBuffer_,
		layoutSaveError_.empty() ? nullptr : layoutSaveError_.c_str());
	if (inputResult.submitted) {

		layoutSaveError_.clear();
		if (context.host->RequestSaveEditorLayout(layoutNameBuffer_, layoutSaveError_)) {
			ImGui::CloseCurrentPopup();
		}
	}
	if (inputResult.canceled) {

		layoutSaveError_.clear();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}
