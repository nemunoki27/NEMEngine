#include "MenuBarPanel.h"
#include <Engine/Editor/Build/EditorGameBuildMenu.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Commands/Entity/DeleteEntityCommand.h>
#include <Engine/Core/Rendering/Core/RenderingPlatform.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Foundation/Time/FrameRateSettings.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <EditorBuildInfo.generated.h>

// c++
#include <algorithm>
#include <iterator>
#include <string>

namespace {

	// 直前のグラフィックス設定項目に説明を表示する
	void DrawGraphicsTooltip(const char* text) {

		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", text);
		}
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
	//	製品ビルド
	//============================================================================
	EditorGameBuildMenu::DrawMenu(context, *context.gameBuildSession);

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

		auto& featureController = context.graphicsPlatform->GetFeatureController();
		const auto& adapterInfo = featureController.GetAdapterInfo();
		const auto& support = featureController.GetSupport();
		const auto& preferences = featureController.GetPreferences();
		const auto& runtime = featureController.GetRuntimeFeatures();
		const double vramGB = static_cast<double>(adapterInfo.dedicatedVideoMemoryBytes) / (1024.0 * 1024.0 * 1024.0);

		if (ImGui::BeginMenu("GPU情報")) {
			ImGui::TextWrapped("Adapter: %s", adapterInfo.adapterName.empty() ? "Unknown" : adapterInfo.adapterName.c_str());
			ImGui::Text("Feature Level : %s", GraphicsFeatureText::ToString(adapterInfo.featureLevel));
			ImGui::Text("Shader Model  : %s", GraphicsFeatureText::ToString(support.highestShaderModel));
			ImGui::Text("Dedicated VRAM: %.2f GB", vramGB);
			ImGui::Separator();
			ImGui::Text("Mesh Shader Tier: %s", GraphicsFeatureText::ToString(support.meshShaderTier));
			ImGui::Text("RayTracing Tier : %s", GraphicsFeatureText::ToString(support.raytracingTier));
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("描画パス")) {
			bool allowMeshShader = preferences.allowMeshShader;
			ImGui::BeginDisabled(!support.SupportsMeshShaderPath());
			if (ImGui::Checkbox("メッシュシェーダーを使用", &allowMeshShader)) {
				featureController.SetAllowMeshShader(allowMeshShader);
			}
			DrawGraphicsTooltip("対応GPUではMesh Shader経路を使用します");
			ImGui::EndDisabled();

			if (!support.SupportsMeshShaderPath()) {
				ImGui::TextDisabled("メッシュシェーダーに対応していないGPUです");
			}
			ImGui::Text("現在のメッシュパス: %s",
				runtime.useMeshShader ? "メッシュシェーダー" : "頂点シェーダー");
			ImGui::EndMenu();
		}

		/*if (ImGui::BeginMenu("フレーム設定")) {
			FrameRateSettings& frameRate = FrameRateSettings::GetInstance();
			const uint32_t fpsOptions[] = {
				30u, 60u, 75u, 90u, 120u, 0u
			};
			const char* fpsLabels[] = {
				"30", "60", "75", "90", "120", "未制限"
			};
			constexpr int fpsOptionCount =
				static_cast<int>(std::size(fpsOptions));
			const auto drawFpsLimit = [&](const char* label,
				uint32_t currentFps, const auto& setter) {

				int fpsIndex = 1;
				for (int i = 0; i < fpsOptionCount; ++i) {
					if (fpsOptions[i] == currentFps) {
						fpsIndex = i;
						break;
					}
				}
				if (ImGui::Combo(label, &fpsIndex,
					fpsLabels, fpsOptionCount)) {

					setter(fpsOptions[fpsIndex]);
					frameRate.Save();
				}
			};

			drawFpsLimit("エディターFPS制限",
				frameRate.GetEditorTargetFps(),
				[&](uint32_t fps) {
					frameRate.SetEditorTargetFps(fps);
				});
			DrawGraphicsTooltip("編集時のGPU過負荷を防ぐ上限です。未制限ではGPU温度により性能が低下する場合があります");
			drawFpsLimit("ゲームFPS制限",
				frameRate.GetTargetFps(),
				[&](uint32_t fps) {
					frameRate.SetTargetFps(fps);
				});
			DrawGraphicsTooltip("製品ランタイムで使用する上限です。未制限はVSyncとCPU側の待機を無効にします");

			const char* frameContextLabels[] = { "1", "2", "3" };
			int frameContextIndex =
				static_cast<int>(preferences.frameContextCount - 1);
			if (ImGui::Combo("FrameContext数", &frameContextIndex,
				frameContextLabels, 3)) {
				featureController.SetFrameContextCount(
					static_cast<uint32_t>(frameContextIndex + 1));
			}
			DrawGraphicsTooltip("CPUが先行できるフレーム数です。変更は再起動後に反映されます");
			ImGui::Text("現在: %u",
				GraphicsFrameState::GetActiveCount());
			if (preferences.frameContextCount !=
				GraphicsFrameState::GetActiveCount()) {
				ImGui::TextDisabled("再起動後に%uへ変更",
					preferences.frameContextCount);
			}
			ImGui::EndMenu();
		}*/

		if (ImGui::BeginMenu("表示出力")) {
			const char* outputModeLabels[] = { "SDR", "HDR10", "scRGB" };
			int outputMode = static_cast<int>(preferences.displayOutput.mode);
			if (ImGui::Combo("出力モード", &outputMode,
				outputModeLabels, 3)) {
				featureController.SetDisplayOutputMode(
					static_cast<DisplayOutputMode>(outputMode));
			}
			DrawGraphicsTooltip("製品ランタイムのSwapChain形式と色空間を変更します");

			float paperWhiteNits =
				preferences.displayOutput.paperWhiteNits;
			float maxLuminanceNits =
				preferences.displayOutput.maxLuminanceNits;
			if (ImGui::DragFloat("Paper White", &paperWhiteNits,
				1.0f, 80.0f, 1000.0f, "%.0f nits")) {
				featureController.SetDisplayLuminance(
					paperWhiteNits,
					(std::max)(paperWhiteNits, maxLuminanceNits));
			}
			DrawGraphicsTooltip("拡散白として扱う表示輝度です");
			if (ImGui::DragFloat("最大輝度", &maxLuminanceNits,
				1.0f, paperWhiteNits, 10000.0f, "%.0f nits")) {
				featureController.SetDisplayLuminance(
					paperWhiteNits, maxLuminanceNits);
			}
			DrawGraphicsTooltip("HDR10メタデータと最終出力変換に使用します");
			ImGui::TextDisabled("NEMEditorはSDR固定、変更は製品ランタイム再起動後に反映");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("カリング")) {
			bool allowFrustumCulling =
				preferences.allowFrustumCulling;
			if (ImGui::Checkbox("視錐台カリング",
				&allowFrustumCulling)) {
				featureController.SetAllowFrustumCulling(
					allowFrustumCulling);
			}
			DrawGraphicsTooltip("カメラの視錐台外にあるインスタンスを描画対象から除外します");

			bool allowOcclusionCulling =
				preferences.allowOcclusionCulling;
			if (ImGui::Checkbox("オクルージョンカリング",
				&allowOcclusionCulling)) {
				featureController.SetAllowOcclusionCulling(
					allowOcclusionCulling);
			}
			DrawGraphicsTooltip("深度ピラミッドで遮蔽されたインスタンスまたはメッシュレットを除外します");

			bool allowContributionCulling =
				preferences.allowContributionCulling;
			if (ImGui::Checkbox("寄与度カリング",
				&allowContributionCulling)) {
				featureController.SetAllowContributionCulling(
					allowContributionCulling);
			}
			DrawGraphicsTooltip("画面上で極端に小さいメッシュを除外します");

			bool allowNormalConeCulling =
				preferences.allowNormalConeCulling;
			ImGui::BeginDisabled(!runtime.useMeshShader);
			if (ImGui::Checkbox("法線コーンカリング",
				&allowNormalConeCulling)) {
				featureController.SetAllowNormalConeCulling(
					allowNormalConeCulling);
			}
			DrawGraphicsTooltip("Mesh Shader経路で裏向きのメッシュレットを除外します");
			ImGui::EndDisabled();

			bool useGameViewCameraForSceneCulling =
				preferences.useGameViewCameraForSceneCulling;
			if (ImGui::Checkbox("SceneViewもGameViewカメラでカリング",
				&useGameViewCameraForSceneCulling)) {
				featureController.SetUseGameViewCameraForSceneCulling(
					useGameViewCameraForSceneCulling);
			}
			DrawGraphicsTooltip("無効時はSceneView自身のカメラでカリングします");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("LOD")) {
			bool allowMeshLOD = preferences.allowMeshLOD;
			if (ImGui::Checkbox("LODを使用",
				&allowMeshLOD)) {
				featureController.SetAllowMeshLOD(
					allowMeshLOD);
			}
			DrawGraphicsTooltip("無効時は常にLOD0を描画します");

			float lod0 = preferences.meshLOD0PixelThreshold;
			float lod1 = preferences.meshLOD1PixelThreshold;
			float lod2 = preferences.meshLOD2PixelThreshold;
			ImGui::BeginDisabled(!allowMeshLOD);
			if (ImGui::DragFloat("LOD0からLOD1", &lod0,
				1.0f,
				lod1 + GraphicsMeshLOD::kPixelThresholdGap,
				GraphicsMeshLOD::kMaximumPixelThreshold,
				"%.1f px")) {
				featureController.SetMeshLODThresholds(
					lod0, lod1, lod2);
			}
			DrawGraphicsTooltip("投影半径がこのピクセル数未満になるとLOD1へ切り替えます");
			if (ImGui::DragFloat("LOD1からLOD2", &lod1,
				1.0f,
				lod2 + GraphicsMeshLOD::kPixelThresholdGap,
				lod0 - GraphicsMeshLOD::kPixelThresholdGap,
				"%.1f px")) {
				featureController.SetMeshLODThresholds(
					lod0, lod1, lod2);
			}
			DrawGraphicsTooltip("投影半径がこのピクセル数未満になるとLOD2へ切り替えます");
			if (ImGui::DragFloat("LOD2からLOD3", &lod2,
				1.0f,
				GraphicsMeshLOD::kMinimumPixelThreshold,
				lod1 - GraphicsMeshLOD::kPixelThresholdGap,
				"%.1f px")) {
				featureController.SetMeshLODThresholds(
					lod0, lod1, lod2);
			}
			DrawGraphicsTooltip("投影半径がこのピクセル数未満になるとLOD3へ切り替えます");
			ImGui::EndDisabled();
			if (ImGui::Button("既定値に戻す")) {

				featureController.SetMeshLODThresholds(
					GraphicsMeshLOD::kDefaultPixelThresholds[0],
					GraphicsMeshLOD::kDefaultPixelThresholds[1],
					GraphicsMeshLOD::kDefaultPixelThresholds[2]);
			}
			DrawGraphicsTooltip("LOD切り替え閾値を160 / 80 / 32 pxへ戻します");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("レイトレーシング")) {
			bool allowInlineRayTracing =
				preferences.allowInlineRayTracing;
			ImGui::BeginDisabled(!support.SupportsRayTracingPath());
			if (ImGui::Checkbox("インラインシャドウ",
				&allowInlineRayTracing)) {
				featureController.SetAllowInlineRayTracing(
					allowInlineRayTracing);
			}
			DrawGraphicsTooltip("RayQueryを使ったシャドウ判定を有効にします");

			const char* shadowSampleLabels[] = {
				"低負荷 (1レイ)", "標準 (2レイ)", "高品質 (4レイ)"
			};
			int shadowSampleIndex =
				preferences.softShadowSampleCount <= 1u ? 0 :
				preferences.softShadowSampleCount <= 2u ? 1 : 2;
			ImGui::BeginDisabled(!allowInlineRayTracing);
			if (ImGui::Combo("ソフトシャドウ品質",
				&shadowSampleIndex, shadowSampleLabels, 3)) {

				const uint32_t sampleCounts[] = { 1u, 2u, 4u };
				featureController.SetSoftShadowSampleCount(
					sampleCounts[shadowSampleIndex]);
			}
			DrawGraphicsTooltip("ライトごとの影レイ数です。1レイでも画素ごとに分散して柔らかい境界を維持します");
			ImGui::EndDisabled();

			bool allowDispatchRays =
				preferences.allowDispatchRays;
			if (ImGui::Checkbox("マテリアル反射パス",
				&allowDispatchRays)) {
				featureController.SetAllowDispatchRays(
					allowDispatchRays);
			}
			DrawGraphicsTooltip("DispatchRaysによる反射描画を有効にします");

			bool allowRaytracingDownsampling =
				preferences.allowRaytracingDownsampling;
			ImGui::BeginDisabled(!allowDispatchRays);
			if (ImGui::Checkbox("ダウンサンプリング",
				&allowRaytracingDownsampling)) {

				featureController.SetAllowRaytracingDownsampling(
					allowRaytracingDownsampling);
			}
			DrawGraphicsTooltip(
				"DispatchRaysと後段フィルターを縮小解像度で実行します");
			ImGui::EndDisabled();
			ImGui::EndDisabled();

			if (!support.SupportsRayTracingPath()) {
				ImGui::TextDisabled("レイトレーシングに対応していないGPUです");
			}
			ImGui::Text("TLASビルド: %s",
				runtime.UsesAnyRayTracing() ? "有効" : "無効");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("デバッグ表示")) {
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

				GBufferDebugView& current =
					context.editorState->gbufferDebugView;
				for (const GBufferDebugItem& item : kItems) {

					bool checked = (current == item.view);
					if (ImGui::Checkbox(item.label, &checked)) {
						// 1つだけ選べるようにし、同じ項目を外したらNoneへ戻す
						current = checked ?
							item.view : GBufferDebugView::None;
					}
				}
				ImGui::Text("現在の表示: %s",
					EnumAdapter<GBufferDebugView>::
					ToString(current));
			}
			ImGui::EndMenu();
		}

		ImGui::SetWindowFontScale(1.0f);

		ImGui::EndMenu();
	}

	//============================================================================
	//	エディターレイアウト設定
	//============================================================================
	DrawEditorLayoutMenu(context);

	// 使用中のSDK情報を表示
	ImGui::TextDisabled("SDKバージョン: %s / ビルド構成: %s",
		EditorBuildInfo::kVersion, EditorBuildInfo::kConfiguration);

	ImGui::SetWindowFontScale(1.0f);

	ImGui::EndMainMenuBar();
	EditorGameBuildMenu::DrawPopup(context, *context.gameBuildSession);
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
