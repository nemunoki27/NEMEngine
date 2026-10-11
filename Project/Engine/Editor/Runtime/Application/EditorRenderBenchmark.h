#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Framework/EngineFramework.h>
#include <filesystem>
#include <chrono>
#include <json.hpp>

namespace Engine {

	class GraphicsFeatureController;
	class EngineApplication;

	//============================================================================
	//	EditorRenderBenchmark class
	//	同じEditorでMSとVSの定常フレームを比較する
	//============================================================================
	class EditorRenderBenchmark final : public IEngineApplication {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		EditorRenderBenchmark(std::unique_ptr<EngineApplication> application, std::filesystem::path output);
		~EditorRenderBenchmark() override;
		void Init(GraphicsCore& graphicsCore) override;
		void Tick(GraphicsCore& graphicsCore, float deltaTime) override;
		void Render(GraphicsCore& graphicsCore) override;
		void RenderPlatformWindows(GraphicsCore& graphicsCore) override;
		void Finalize() override;
		bool ConsumeFrameDeltaResetRequest() override;
		bool UsesEditorUI() const override { return true; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 通常Editorへの委譲先と結果の保存先
		std::unique_ptr<EngineApplication> application_;
		std::filesystem::path output_;
		// 計測終了時に元の描画設定へ戻す
		GraphicsFeatureController* features_ = nullptr;
		bool originalMeshShader_ = false;
		// 画像比較時だけScene内の時刻を固定する
		bool freezeAnimation_ = false;
		// 切替後の読込とフレーム安定を待つ
		uint32_t phase_ = 0;
		uint32_t warmFrames_ = 0;
		uint64_t textureRevision_ = 0;
		std::chrono::steady_clock::time_point phaseStarted_;
		nlohmann::json report_;
		nlohmann::json frames_ = nlohmann::json::array();

		//--------- functions ----------------------------------------------------

		// 描画経路を切り替えて計測待機を開始する
		void BeginPhase();
		// 今回の結果を保存して次の描画経路へ進む
		void FinishPhase(GraphicsCore& graphicsCore);
		// 計測終了後のViewを画像へ保存する
		void CaptureViews(GraphicsCore& graphicsCore);
	};
}
