#include "RuntimeSystemRegistration.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>
#include <Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>
#include <Engine/Core/World/Systems/Animation/AnimationPlayerSystem.h>
#include <Engine/Core/World/Systems/Animation/JointAttachmentSystem.h>
#include <Engine/Core/World/Systems/Animation/SkinnedAnimationSystem.h>
#include <Engine/Core/World/Systems/Audio/AudioSourceSystem.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>
#include <Engine/Core/World/Systems/Camera/CameraControllerSystem.h>
#include <Engine/Core/World/Systems/Camera/CameraShakeSystem.h>
#include <Engine/Core/World/Systems/Effect/ParticleSystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Physics/CollisionSystem.h>
#include <Engine/Core/World/Systems/Physics/PhysicsSystem.h>
#include <Engine/Core/World/Systems/Rendering/FlipbookAnimationSystem.h>
#include <Engine/Core/World/Systems/Rendering/UVTransformSystem.h>
#include <Engine/Core/World/Systems/Transform/TransformSystem.h>
#include <Engine/Core/World/Systems/UI/UICanvasSystem.h>
#include <Engine/Core/World/Systems/UI/UIInputSystem.h>

namespace {

	//============================================================================
	//	RenderFeatureProfileSystem class
	//	Play中のアクティブシーンとRenderFeatureProfileを同期するシステム
	//============================================================================
	class RenderFeatureProfileSystem final :
		public Engine::ISystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderFeatureProfileSystem() = default;
		~RenderFeatureProfileSystem() = default;

		void OnWorldEnter(Engine::ECSWorld& world, Engine::SystemContext& context) override;
		void OnSceneInstancesChanged(Engine::ECSWorld& world,
			Engine::SystemContext& context, Engine::SceneChangePhase phase) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "RenderFeatureProfileSystem"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		// アクティブシーンのProfileをC# Lifecycle処理より先に反映する
		void Synchronize(Engine::SystemContext& context);
	};
}

//============================================================================
//	RenderFeatureProfileSystem classMethods
//============================================================================
void RenderFeatureProfileSystem::OnWorldEnter(
	[[maybe_unused]] Engine::ECSWorld& world, Engine::SystemContext& context) {

	Synchronize(context);
}

void RenderFeatureProfileSystem::OnSceneInstancesChanged(
	[[maybe_unused]] Engine::ECSWorld& world, Engine::SystemContext& context,
	[[maybe_unused]] Engine::SceneChangePhase phase) {

	Synchronize(context);
}

void RenderFeatureProfileSystem::Synchronize(Engine::SystemContext& context) {

	if (context.mode != Engine::WorldMode::Play ||
		!context.activeSceneHeader || !context.assetDatabase) {

		return;
	}

	Engine::RenderFeatureProfileService::GetInstance().SetActiveProfileAsset(
		context.activeSceneHeader->renderFeatureProfile, context.assetDatabase);
}

//============================================================================
//	RuntimeSystemRegistration functions
//============================================================================
Engine::UIInputSystem* Engine::RegisterRuntimeSystems(SystemScheduler& scheduler) {

	int32_t order = 0;
	scheduler.AddSystem(std::make_unique<HierarchySystem>(), ++order);
	// ScriptのAwakeとStartから新しいシーンのProfileを参照できるようにする
	scheduler.AddSystem(std::make_unique<RenderFeatureProfileSystem>(), ++order);

	// UI入力はBehaviorより先に確定し、C#のUpdateから同フレームの入力を参照できるようにする
	auto uiInputSystem = std::make_unique<UIInputSystem>();
	UIInputSystem* result = uiInputSystem.get();
	scheduler.AddSystem(std::move(uiInputSystem), ++order);

	scheduler.AddSystem(std::make_unique<BehaviorSystem>(), ++order);
	scheduler.AddSystem(std::make_unique<AnimationPlayerSystem>(), ++order);
	scheduler.AddSystem(std::make_unique<PhysicsSystem>(), ++order);
	scheduler.AddSystem(std::make_unique<AudioSourceSystem>(), ++order);
	scheduler.AddSystem(std::make_unique<CameraControllerSystem>(), ++order);
	scheduler.AddSystem(std::make_unique<CameraShakeSystem>(), ++order);
	scheduler.AddSystem(std::make_unique<TransformSystem>(), ++order);
	scheduler.AddSystem(std::make_unique<ParticleSystem>(), ++order);
	scheduler.AddSystem(std::make_unique<CollisionSystem>(), ++order);
	scheduler.AddSystem(std::make_unique<FlipbookAnimationSystem>(), ++order);
	scheduler.AddSystem(std::make_unique<UVTransformSystem>(), ++order);
	scheduler.AddSystem(std::make_unique<SkinnedAnimationSystem>(), ++order);
	// ジョイント追従はスケルトン更新後にジョイントのワールド行列を参照する
	scheduler.AddSystem(std::make_unique<JointAttachmentSystem>(), ++order);
	// Canvas行列は全Transform更新後に確定する
	scheduler.AddSystem(std::make_unique<UICanvasSystem>(), ++order);
	return result;
}
