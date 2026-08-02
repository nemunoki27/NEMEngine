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
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		AssetID activeScene_{};
		AssetDatabase assetDataBase_;

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

		void InitSystems();
		void LoadActiveSceneConfig();
		void SaveActiveSceneConfig() const;
		void InitFirstScene();
		void StartPlayWorld();
		void StopPlayWorld();
		void RefreshActiveWorldContext();
		bool HandleApplicationQuitRequest();

		const SceneHeader* GetActiveSceneHeader() const;
		RenderFrameRequest BuildRenderFrameRequest(GraphicsCore& graphicsCore);

		void PreloadReleaseResources(GraphicsCore& graphicsCore);
		void WarmupReleaseWorld(GraphicsCore& graphicsCore, ECSWorld& world,
			SceneInstanceManager& scenes, SystemContext& context);
	};
} // Engine
