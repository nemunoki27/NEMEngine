#include "EditorGraphicsMenu.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Rendering/Core/RenderingPlatform.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>

namespace {

	// 直前の設定項目に説明を表示する
	void DrawGraphicsTooltip(const char* text) {

		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", text);
		}
	}
}

//============================================================================
//	EditorGraphicsMenu functions
//============================================================================

void Engine::EditorGraphicsMenu::Draw(const EditorPanelContext& context) {

	if (ImGui::BeginMenu("グラフィックス設定")) {

		ImGui::SetWindowFontScale(0.8f);

		// GPUの対応状況と希望設定を取得
		auto& featureController = context.graphicsPlatform->GetFeatureController();
		const auto& adapterInfo = featureController.GetAdapterInfo();
		const auto& support = featureController.GetSupport();
		const auto& preferences = featureController.GetPreferences();
		const auto& runtime = featureController.GetRuntimeFeatures();
		const double vramGB = static_cast<double>(adapterInfo.dedicatedVideoMemoryBytes) / (1024.0 * 1024.0 * 1024.0);

		// 使用中のGPUと対応機能を表示
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

		// 希望するMeshの描画経路を設定
		if (ImGui::BeginMenu("描画パス")) {
			bool allowMeshShader = preferences.allowMeshShader;
			if (ImGui::Checkbox("メッシュシェーダーを使用", &allowMeshShader)) {
				featureController.SetAllowMeshShader(allowMeshShader);
			}
			DrawGraphicsTooltip("対応GPUではMesh Shader経路を使用します");

			if (!support.SupportsMeshShaderPath()) {
				ImGui::TextDisabled("未対応GPUでは実行時だけ頂点シェーダーへ切り替えます");
			}
			ImGui::Text("現在のメッシュパス: %s", runtime.useMeshShader ? "メッシュシェーダー" : "頂点シェーダー");
			ImGui::EndMenu();
		}

		// 製品の出力形式と輝度を設定
		if (ImGui::BeginMenu("表示出力")) {
			const char* outputModeLabels[] = {"SDR", "HDR10", "scRGB"};
			int outputMode = static_cast<int>(preferences.displayOutput.mode);
			if (ImGui::Combo("出力モード", &outputMode, outputModeLabels, 3)) {
				featureController.SetDisplayOutputMode(static_cast<DisplayOutputMode>(outputMode));
			}
			DrawGraphicsTooltip("製品ランタイムのSwapChain形式と色空間を変更します");

			float paperWhiteNits = preferences.displayOutput.paperWhiteNits;
			float maxLuminanceNits = preferences.displayOutput.maxLuminanceNits;
			if (ImGui::DragFloat("Paper White", &paperWhiteNits, 1.0f, 80.0f, 1000.0f, "%.0f nits")) {
				featureController.SetDisplayLuminance(paperWhiteNits, (std::max)(paperWhiteNits, maxLuminanceNits));
			}
			DrawGraphicsTooltip("拡散白として扱う表示輝度です");
			if (ImGui::DragFloat("最大輝度", &maxLuminanceNits, 1.0f, paperWhiteNits, 10000.0f, "%.0f nits")) {
				featureController.SetDisplayLuminance(paperWhiteNits, maxLuminanceNits);
			}
			DrawGraphicsTooltip("HDR10メタデータと最終出力変換に使用します");
			ImGui::TextDisabled("NEMEditorはSDR固定、変更は製品ランタイム再起動後に反映");
			ImGui::EndMenu();
		}

		// 描画対象を絞る条件を設定
		if (ImGui::BeginMenu("カリング")) {
			bool allowFrustumCulling = preferences.allowFrustumCulling;
			if (ImGui::Checkbox("視錐台カリング", &allowFrustumCulling)) {
				featureController.SetAllowFrustumCulling(allowFrustumCulling);
			}
			DrawGraphicsTooltip("カメラの視錐台外にあるインスタンスを描画対象から除外します");

			bool allowOcclusionCulling = preferences.allowOcclusionCulling;
			if (ImGui::Checkbox("オクルージョンカリング", &allowOcclusionCulling)) {
				featureController.SetAllowOcclusionCulling(allowOcclusionCulling);
			}
			DrawGraphicsTooltip("深度ピラミッドで遮蔽されたインスタンスまたはメッシュレットを除外します");

			bool allowContributionCulling = preferences.allowContributionCulling;
			if (ImGui::Checkbox("寄与度カリング", &allowContributionCulling)) {
				featureController.SetAllowContributionCulling(allowContributionCulling);
			}
			DrawGraphicsTooltip("画面上で極端に小さいメッシュを除外します");

			bool allowNormalConeCulling = preferences.allowNormalConeCulling;
			ImGui::BeginDisabled(!runtime.useMeshShader);
			if (ImGui::Checkbox("法線コーンカリング", &allowNormalConeCulling)) {
				featureController.SetAllowNormalConeCulling(allowNormalConeCulling);
			}
			DrawGraphicsTooltip("Mesh Shader経路で裏向きのメッシュレットを除外します");
			ImGui::EndDisabled();

			bool useGameViewCameraForSceneCulling = preferences.useGameViewCameraForSceneCulling;
			if (ImGui::Checkbox("SceneViewもGameViewカメラでカリング", &useGameViewCameraForSceneCulling)) {
				featureController.SetUseGameViewCameraForSceneCulling(useGameViewCameraForSceneCulling);
			}
			DrawGraphicsTooltip("無効時はSceneView自身のカメラでカリングします");

			ImGui::EndMenu();
		}

		// 投影サイズによるLODの切替条件を設定
		if (ImGui::BeginMenu("LOD")) {
			bool allowMeshLOD = preferences.allowMeshLOD;
			if (ImGui::Checkbox("LODを使用", &allowMeshLOD)) {
				featureController.SetAllowMeshLOD(allowMeshLOD);
			}
			DrawGraphicsTooltip("無効時は常にLOD0を描画します");

			float lod0 = preferences.meshLOD0PixelThreshold;
			float lod1 = preferences.meshLOD1PixelThreshold;
			float lod2 = preferences.meshLOD2PixelThreshold;
			ImGui::BeginDisabled(!allowMeshLOD);
			if (ImGui::DragFloat("LOD0からLOD1", &lod0, 1.0f, lod1 + GraphicsMeshLOD::kPixelThresholdGap,
					GraphicsMeshLOD::kMaximumPixelThreshold, "%.1f px")) {
				featureController.SetMeshLODThresholds(lod0, lod1, lod2);
			}
			DrawGraphicsTooltip("投影半径がこのピクセル数未満になるとLOD1へ切り替えます");
			if (ImGui::DragFloat("LOD1からLOD2", &lod1, 1.0f, lod2 + GraphicsMeshLOD::kPixelThresholdGap,
					lod0 - GraphicsMeshLOD::kPixelThresholdGap, "%.1f px")) {
				featureController.SetMeshLODThresholds(lod0, lod1, lod2);
			}
			DrawGraphicsTooltip("投影半径がこのピクセル数未満になるとLOD2へ切り替えます");
			if (ImGui::DragFloat("LOD2からLOD3", &lod2, 1.0f, GraphicsMeshLOD::kMinimumPixelThreshold,
					lod1 - GraphicsMeshLOD::kPixelThresholdGap, "%.1f px")) {
				featureController.SetMeshLODThresholds(lod0, lod1, lod2);
			}
			DrawGraphicsTooltip("投影半径がこのピクセル数未満になるとLOD3へ切り替えます");
			ImGui::EndDisabled();
			if (ImGui::Button("既定値に戻す")) {

				featureController.SetMeshLODThresholds(GraphicsMeshLOD::kDefaultPixelThresholds[0],
					GraphicsMeshLOD::kDefaultPixelThresholds[1], GraphicsMeshLOD::kDefaultPixelThresholds[2]);
			}
			DrawGraphicsTooltip("LOD切り替え閾値を160 / 80 / 32 pxへ戻します");
			ImGui::EndMenu();
		}

		// レイトレーシングの影と反射を設定
		if (ImGui::BeginMenu("レイトレーシング")) {
			bool allowInlineRayTracing = preferences.allowInlineRayTracing;
			if (ImGui::Checkbox("インラインシャドウ", &allowInlineRayTracing)) {
				featureController.SetAllowInlineRayTracing(allowInlineRayTracing);
			}
			DrawGraphicsTooltip("RayQueryを使ったシャドウ判定を有効にします");

			const char* shadowSampleLabels[] = {"低負荷 (1レイ)", "標準 (2レイ)", "高品質 (4レイ)"};
			int shadowSampleIndex = preferences.softShadowSampleCount <= 1u	  ? 0
									: preferences.softShadowSampleCount <= 2u ? 1
																			  : 2;
			ImGui::BeginDisabled(!allowInlineRayTracing);
			if (ImGui::Combo("ソフトシャドウ品質", &shadowSampleIndex, shadowSampleLabels, 3)) {

				const uint32_t sampleCounts[] = {1u, 2u, 4u};
				featureController.SetSoftShadowSampleCount(sampleCounts[shadowSampleIndex]);
			}
			DrawGraphicsTooltip("ライトごとの影レイ数です。1レイでも画素ごとに分散して柔らかい境界を維持します");
			ImGui::EndDisabled();

			bool allowDispatchRays = preferences.allowDispatchRays;
			if (ImGui::Checkbox("マテリアル反射パス", &allowDispatchRays)) {
				featureController.SetAllowDispatchRays(allowDispatchRays);
			}
			DrawGraphicsTooltip("DispatchRaysによる反射描画を有効にします");

			bool allowRaytracingDownsampling = preferences.allowRaytracingDownsampling;
			ImGui::BeginDisabled(!allowDispatchRays);
			if (ImGui::Checkbox("ダウンサンプリング", &allowRaytracingDownsampling)) {

				featureController.SetAllowRaytracingDownsampling(allowRaytracingDownsampling);
			}
			DrawGraphicsTooltip("DispatchRaysと後段フィルターを縮小解像度で実行します");
			ImGui::EndDisabled();

			if (!support.SupportsRayTracingPath()) {
				ImGui::TextDisabled("未対応GPUでは実行時だけRayTracingを無効化します");
			}
			ImGui::Text("TLASビルド: %s", runtime.UsesAnyRayTracing() ? "有効" : "無効");
			ImGui::EndMenu();
		}

		// GBufferの確認対象を選択
		if (ImGui::BeginMenu("デバッグ表示")) {
			ImGui::TextDisabled("Deferred GBuffer View");
			if (context.editorState) {

				// GBufferの表示候補
				struct GBufferDebugItem {

					const char* label; // 表示名
					GBufferDebugView view; // 表示対象
				};
				static const GBufferDebugItem kItems[] = {
					{"Albedo", GBufferDebugView::Albedo},
					{"Normal", GBufferDebugView::Normal},
					{"World Pos", GBufferDebugView::Position},
					{"Material", GBufferDebugView::Material},
					{"Emissive", GBufferDebugView::Emissive},
					{"Depth", GBufferDebugView::Depth},
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
			ImGui::EndMenu();
		}

		ImGui::SetWindowFontScale(1.0f);

		ImGui::EndMenu();
	}
}
