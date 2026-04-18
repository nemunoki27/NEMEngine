#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsFeatureTypes.h>

namespace Engine {

	//============================================================================
	//	GraphicsFeatureController class
	//	検出結果/ユーザー設定/実行状態をまとめて管理する
	//============================================================================
	class GraphicsFeatureController {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		GraphicsFeatureController() = default;
		~GraphicsFeatureController() = default;

		// GPUから検出した情報を反映する
		void ApplyDetectedSupport(const GraphicsAdapterInfo& adapterInfo,
			const GraphicsFeatureSupport& support);

		// ユーザー設定変更
		void SetAllowMeshShader(bool enabled);
		void SetAllowInlineRayTracing(bool enabled);
		void SetAllowDispatchRays(bool enabled);

		//--------- accessor -----------------------------------------------------

		const GraphicsAdapterInfo& GetAdapterInfo() const { return adapterInfo_; }
		const GraphicsFeatureSupport& GetSupport() const { return support_; }
		const GraphicsFeaturePreferences& GetPreferences() const { return preferences_; }
		const GraphicsRuntimeFeatures& GetRuntimeFeatures() const { return runtimeFeatures_; }

		bool ShouldUseMeshShader() const { return runtimeFeatures_.useMeshShader; }
		bool ShouldUseInlineRayTracing() const { return runtimeFeatures_.useInlineRayTracing; }
		bool ShouldUseDispatchRays() const { return runtimeFeatures_.useDispatchRays; }
		bool ShouldBuildRaytracingScene() const { return runtimeFeatures_.UsesAnyRayTracing(); }
		bool ShouldUseRayTracing() const { return ShouldBuildRaytracingScene(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

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
	};
} // Engine