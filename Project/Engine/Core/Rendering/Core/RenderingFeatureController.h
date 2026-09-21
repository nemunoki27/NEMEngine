#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingFeatureTypes.h>

namespace Engine {

	//============================================================================
	//	GraphicsFeatureController class
	//	検出結果/ユーザー設定/実行状態をまとめて管理する
	//============================================================================
	class GraphicsFeatureController {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		GraphicsFeatureController() = default;
		~GraphicsFeatureController() = default;

		// GPUから検出した情報を反映する
		void ApplyDetectedSupport(const GraphicsAdapterInfo& adapterInfo,
			const GraphicsFeatureSupport& support);

		// ユーザー設定変更
		void SetAllowMeshShader(bool enabled);
		void SetAllowInlineRayTracing(bool enabled);
		void SetAllowDispatchRays(bool enabled);
		void SetAllowRaytracingDownsampling(bool enabled);
		void SetSoftShadowSampleCount(uint32_t count);
		// 描画カリング機能を個別に切り替える
		void SetAllowFrustumCulling(bool enabled);
		void SetAllowOcclusionCulling(bool enabled);
		void SetUseGameViewCameraForSceneCulling(bool enabled);
		void SetAllowContributionCulling(bool enabled);
		void SetAllowNormalConeCulling(bool enabled);
		// メッシュLODと切り替え閾値を設定する
		void SetAllowMeshLOD(bool enabled);
		void SetMeshLODThresholds(
			float lod0, float lod1, float lod2);
		// 次回起動時のフレームコンテキスト数を設定する
		void SetFrameContextCount(uint32_t count);
		// 製品ランタイムで使用するDisplay出力設定を保存する
		void SetDisplayOutputMode(DisplayOutputMode mode);
		void SetDisplayLuminance(float paperWhiteNits,
			float maxLuminanceNits);

		//--------- accessor -----------------------------------------------------

		const GraphicsAdapterInfo& GetAdapterInfo() const { return adapterInfo_; }
		const GraphicsFeatureSupport& GetSupport() const { return support_; }
		const GraphicsFeaturePreferences& GetPreferences() const { return preferences_; }
		const GraphicsRuntimeFeatures& GetRuntimeFeatures() const { return runtimeFeatures_; }

		bool ShouldUseMeshShader() const { return runtimeFeatures_.useMeshShader; }
		bool ShouldUseInlineRayTracing() const { return runtimeFeatures_.useInlineRayTracing; }
		bool ShouldUseDispatchRays() const { return runtimeFeatures_.useDispatchRays; }
		// 描画側はPreferencesではなくRuntimeFeaturesを参照して最終状態だけを見る
		bool ShouldUseFrustumCulling() const { return runtimeFeatures_.useFrustumCulling; }
		bool ShouldUseOcclusionCulling() const { return runtimeFeatures_.useOcclusionCulling; }
		bool ShouldUseGameViewCameraForSceneCulling() const { return preferences_.useGameViewCameraForSceneCulling; }
		bool ShouldUseContributionCulling() const { return runtimeFeatures_.useContributionCulling; }
		bool ShouldUseNormalConeCulling() const { return runtimeFeatures_.useNormalConeCulling; }
		bool ShouldBuildRaytracingScene() const { return runtimeFeatures_.UsesAnyRayTracing(); }
		bool ShouldUseRayTracing() const { return ShouldBuildRaytracingScene(); }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		GraphicsAdapterInfo adapterInfo_{};
		GraphicsFeatureSupport support_{};
		GraphicsFeaturePreferences preferences_{};
		GraphicsRuntimeFeatures runtimeFeatures_{};

		// 初期化済みか
		bool initialized_ = false;

		//--------- functions ----------------------------------------------------

		// ユーザー設定をサポート状況に合わせて調整する
		void ClampPreferencesToSupport();
		// サポート状況とユーザー設定から、ランタイムで使用する機能を決定する
		void RebuildRuntimeFeatures();
		// 現在の状態をログ出力する
		void LogCurrentState() const;
		// Graphicsメニューのユーザー設定をEngine Assets/Configへ保存、復元する
		void LoadPreferencesFromConfig();
		void SavePreferencesToConfig() const;
	};
} // Engine

