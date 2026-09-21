#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Meshes/Animation/SkinnedMeshAnimationManager.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Runtime/Framework/EngineFramework.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h>
#include <Engine/Core/World/ECS/Baking/RuntimeWorldBaker.h>
#include <Engine/Core/World/ECS/World/WorldManager.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>

namespace Engine {

	//============================================================================
	//	GameApplication class
	//	製品実行時のWorld更新と描画だけを管理する
	//============================================================================
	class GameApplication final : public IEngineApplication {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		GameApplication() = default;
		~GameApplication() override = default;

		void Init(GraphicsCore& graphicsCore) override;
		void Tick(GraphicsCore& graphicsCore, float deltaTime) override;
		void Render(GraphicsCore& graphicsCore) override;
		void RenderPlatformWindows(GraphicsCore& graphicsCore) override;
		void Finalize() override;
		bool ConsumeFrameDeltaResetRequest() override;
		bool UsesEditorUI() const override { return false; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		AssetID activeScene_{};
		AssetDatabase assetDatabase_;

		SceneInstanceManager editScenes_;
		SceneInstanceManager playScenes_;
		SceneSystem sceneSystem_;

		WorldManager worldManager_;
		RuntimeWorldBaker runtimeWorldBaker_;
		SystemScheduler scheduler_;
		SystemContext systemContext_;

		std::unique_ptr<RenderPipelineRunner> renderPipeline_;
		SkinnedMeshAnimationManager skinnedAnimationManager_;
		AnimationClipManager animationClipManager_;

		bool requestFrameDeltaReset_ = false;
		bool playWorldJustStarted_ = false;

		//--------- functions ----------------------------------------------------

		// 実行用システムを登録する
		void InitSystems();
		// 起動シーンの設定を読み込む
		void LoadActiveSceneConfig();
		// 最後に開いたシーンを保存する
		void SaveActiveSceneConfig() const;
		// 起動シーンを編集ワールドへ読み込む
		void InitFirstScene();
		// 保存用データから実行ワールドを開始する
		void StartPlayWorld();
		// 実行ワールドを切り離して破棄する
		void StopPlayWorld();
		// 実行対象をシステムへ接続する
		void RefreshActiveWorldContext();
		// スクリプトからの終了要求を処理する
		bool HandleApplicationQuitRequest();

		// 実行対象シーンのヘッダを取得する
		const SceneHeader* GetActiveSceneHeader() const;
		// ゲームビューの描画要求を構築する
		RenderFrameRequest BuildRenderFrameRequest(GraphicsCore& graphicsCore);

		// Release起動時の事前読み込みを実行する
		void PreloadReleaseResources(GraphicsCore& graphicsCore);
	};
} // Engine
