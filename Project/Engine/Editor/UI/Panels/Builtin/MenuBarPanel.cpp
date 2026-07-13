#include "MenuBarPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Commands/Entity/DeleteEntityCommand.h>
#include <Engine/Core/Rendering/Core/RenderingPlatform.h>
#include <Engine/Core/Foundation/Time/FrameRateSettings.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// c++
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

	if (!ImGui::BeginMainMenuBar()) {
		return;
	}

	ImGui::SetWindowFontScale(0.85f);

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

		// フレームレート上限はここで切り替えて.exeConfigへ保存する、0は制限なし
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
	DrawLayoutSavePopup(context);
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
