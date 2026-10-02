#include "RuntimeSystemRegistration.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h>
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
#include <Engine/Core/World/Systems/Physics/JointConstraintSystem.h>
#include <Engine/Core/World/Systems/Rendering/FlipbookAnimationSystem.h>
#include <Engine/Core/World/Systems/Rendering/UVTransformSystem.h>
#include <Engine/Core/World/Systems/Transform/TransformSystem.h>
#include <Engine/Core/World/Systems/UI/UICanvasSystem.h>
#include <Engine/Core/World/Systems/UI/UIInputSystem.h>

//============================================================================
//	RuntimeSystemRegistration functions
//============================================================================
Engine::UIInputSystem* Engine::RegisterRuntimeSystems(SystemScheduler& scheduler) {

	int32_t order = 0;
	scheduler.AddSystem(std::make_unique<HierarchySystem>(), ++order);
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
	// Transform更新後の座標でJoint拘束を解く
	scheduler.AddSystem(std::make_unique<JointConstraintSystem>(), ++order);
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
