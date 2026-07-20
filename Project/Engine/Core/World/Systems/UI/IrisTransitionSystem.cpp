#include "IrisTransitionSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/UI/UIRuntimeService.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/Runtime/Context/EngineContext.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	IrisTransitionSystem internal
//============================================================================
namespace {

	constexpr int32_t kTransitionSortingLayer = 1000000;
	constexpr int32_t kTransitionOrder = 1000000;
	constexpr float kProgressEpsilon = 0.00001f;

	bool IsSameProgress(float lhs, float rhs) {

		return std::abs(lhs - rhs) <= kProgressEpsilon;
	}
}

//============================================================================
//	IrisTransitionSystem classMethods
//============================================================================
void Engine::IrisTransitionSystem::OnWorldEnter(ECSWorld& world,
	[[maybe_unused]] SystemContext& context) {

	ResetPlayback();
	activeSceneInstanceID_ = {};
	editCommandPreview_ = false;
	editAuthoringProgress_ = 0.0f;
	editPreviewSerial_ = 0;
	EnsureRenderEntity(world);
	UIRuntimeService::GetInstance().SetTransitionInputBlocked(false);
	if (SceneInstanceManager* sceneInstances =
		world.GetCommandServices().sceneInstances) {

		sceneInstances->ClearSingleLoadRequest();
	}
}

void Engine::IrisTransitionSystem::OnWorldExit(ECSWorld& world,
	[[maybe_unused]] SystemContext& context) {

	UIRuntimeService::GetInstance().SetTransitionInputBlocked(false);
	DestroyRenderEntity(world);
	ResetPlayback();
	activeSceneInstanceID_ = {};
	editCommandPreview_ = false;
	editPreviewSerial_ = 0;
	if (SceneInstanceManager* sceneInstances =
		world.GetCommandServices().sceneInstances) {

		sceneInstances->ClearSingleLoadRequest();
	}
}

void Engine::IrisTransitionSystem::Update(ECSWorld& world, SystemContext& context) {

	EnsureRenderEntity(world);
	ConsumeLatestCommand(world, context.mode);
	if (context.mode == WorldMode::Play) {
		RefreshOwnerSettings(world);
	}
	bool sceneTransitionHandled = false;
	if (context.mode == WorldMode::Play) {
		sceneTransitionHandled = UpdateSceneTransition(world);
	}

	bool editPreviewVisible = false;
	if (context.mode == WorldMode::Edit) {
		editPreviewVisible = UpdateEditPreview(world);
		inputBlocked_ = false;
	} else {
		editCommandPreview_ = false;
	}

	const float deltaTime = settings_.useUnscaledTime ?
		context.unscaledDeltaTime : context.deltaTime;
	UpdatePlayback(sceneTransitionHandled ? 0.0f : deltaTime);

	const bool visible = context.mode == WorldMode::Edit ?
		editPreviewVisible && (state_ != IrisTransitionState::Open || 0.0f < progress_) :
		state_ != IrisTransitionState::Open || 0.0f < progress_;
	UpdateRenderEntity(world, visible);
	SyncComponentRuntime(world);

	if (state_ == IrisTransitionState::Open) {
		inputBlocked_ = false;
	}
	UIRuntimeService::GetInstance().SetTransitionInputBlocked(
		context.mode == WorldMode::Play && inputBlocked_);
	if (SceneInstanceManager* sceneInstances =
		world.GetCommandServices().sceneInstances;
		sceneInstances && (context.mode != WorldMode::Play || !IsTransitionActive())) {

		sceneInstances->ClearSingleLoadRequest();
	}
}

void Engine::IrisTransitionSystem::EnsureRenderEntity(ECSWorld& world) {

	if (world.IsAlive(renderEntity_)) {
		return;
	}

	renderEntity_ = world.CreateEntity();
	world.AddComponent<TransformComponent>(renderEntity_);

	auto& canvas = world.AddComponent<CanvasComponent>(renderEntity_);
	canvas.scaleMode = CanvasScaleMode::ConstantPixelSize;
	canvas.scaleFactor = 1.0f;
	canvas.sortingLayer = kTransitionSortingLayer;
	canvas.order = kTransitionOrder;
	canvas.blockGameplayInput = false;
	canvas.inputInEditMode = false;
	canvas.keyboardInputEnabled = false;
	canvas.gamepadInputEnabled = false;

	auto& sprite = world.AddComponent<SpriteRendererComponent>(renderEntity_);
	sprite.material = BuiltinAssets::Materials::IrisTransition;
	sprite.pivot = Vector2(0.0f, 0.0f);
	sprite.layer = 0;
	sprite.order = 0;
	sprite.visible = false;
	sprite.blendMode = BlendMode::Normal;
	sprite.queue = RenderPhase::ScreenUI;
}

void Engine::IrisTransitionSystem::DestroyRenderEntity(ECSWorld& world) {

	if (!world.IsAlive(renderEntity_)) {
		renderEntity_ = Entity::Null();
		return;
	}
	world.DestroyEntity(renderEntity_);
	world.FlushPendingDestroyEntities();
	renderEntity_ = Entity::Null();
}

bool Engine::IrisTransitionSystem::ConsumeLatestCommand(ECSWorld& world, WorldMode mode) {

	Entity commandEntity = Entity::Null();
	IrisTransitionCommand command = IrisTransitionCommand::None;
	float commandValue = 0.0f;
	uint64_t commandSerial = 0;

	world.ForEach<IrisTransitionComponent>([&](Entity entity,
		IrisTransitionComponent& component) {

		if (component.runtimeCommand != IrisTransitionCommand::None &&
			commandSerial <= component.runtimeCommandSerial) {
			commandEntity = entity;
			command = component.runtimeCommand;
			commandValue = component.runtimeCommandValue;
			commandSerial = component.runtimeCommandSerial;
		}
		component.runtimeCommand = IrisTransitionCommand::None;
		component.runtimeCommandValue = 0.0f;
		});

	if (!world.IsAlive(commandEntity) || command == IrisTransitionCommand::None) {
		return false;
	}

	auto& component = world.GetComponent<IrisTransitionComponent>(commandEntity);
	CopySettings(component);
	ownerUUID_ = world.GetUUID(commandEntity);
	if (mode == WorldMode::Edit) {
		component.runtimeEditPreviewSerial = commandSerial;
		editPreviewSerial_ = commandSerial;
		editCommandPreview_ = component.previewInEditMode;
		editAuthoringProgress_ = component.previewProgress;
	}

	switch (command) {
	case IrisTransitionCommand::IrisOut:
		inputBlocked_ = settings_.blockInput;
		StartPlayback(IrisTransitionState::IrisOut, 1.0f,
			settings_.irisOutDuration, settings_.irisOutEasing);
		break;
	case IrisTransitionCommand::IrisIn:
		pendingSceneTransition_ = false;
		inputBlocked_ = settings_.blockInput;
		StartPlayback(IrisTransitionState::IrisIn, 0.0f,
			settings_.irisInDuration, settings_.irisInEasing);
		break;
	case IrisTransitionCommand::SetProgress:
		pendingSceneTransition_ = false;
		progress_ = std::clamp(commandValue, 0.0f, 1.0f);
		state_ = progress_ <= kProgressEpsilon ? IrisTransitionState::Open :
			1.0f - kProgressEpsilon <= progress_ ? IrisTransitionState::Covered :
			IrisTransitionState::Stopped;
		inputBlocked_ = false;
		break;
	case IrisTransitionCommand::Cancel:
		pendingSceneTransition_ = false;
		state_ = progress_ <= kProgressEpsilon ? IrisTransitionState::Open :
			1.0f - kProgressEpsilon <= progress_ ? IrisTransitionState::Covered :
			IrisTransitionState::Stopped;
		inputBlocked_ = false;
		break;
	case IrisTransitionCommand::Reset:
		ResetPlayback();
		if (mode == WorldMode::Edit) {
			ownerUUID_ = world.GetUUID(commandEntity);
			editCommandPreview_ = component.previewInEditMode;
		}
		break;
	case IrisTransitionCommand::None:
	default:
		break;
	}
	return true;
}

bool Engine::IrisTransitionSystem::UpdateSceneTransition(ECSWorld& world) {

	SceneInstanceManager* sceneInstances = world.GetCommandServices().sceneInstances;
	const SceneInstance* activeScene = sceneInstances ? sceneInstances->GetActive() : nullptr;
	if (!activeScene) {
		return false;
	}
	if (!activeSceneInstanceID_) {
		activeSceneInstanceID_ = activeScene->instanceID;
		return false;
	}
	if (activeSceneInstanceID_ == activeScene->instanceID) {
		if (!pendingSceneTransition_) {
			return false;
		}
	} else {
		activeSceneInstanceID_ = activeScene->instanceID;
		pendingSceneTransition_ =
			state_ == IrisTransitionState::IrisOut;
	}

	if (state_ != IrisTransitionState::Covered) {
		if (state_ != IrisTransitionState::IrisOut) {
			pendingSceneTransition_ = false;
		}
		return false;
	}
	pendingSceneTransition_ = false;
	if (!settings_.autoIrisInAfterSceneTransition) {
		ResetPlayback();
		return true;
	}

	inputBlocked_ = settings_.blockInput;
	StartPlayback(IrisTransitionState::IrisIn, 0.0f,
		settings_.irisInDuration, settings_.irisInEasing);
	return true;
}

bool Engine::IrisTransitionSystem::UpdateEditPreview(ECSWorld& world) {

	Entity previewEntity = Entity::Null();
	uint64_t previewSerial = 0;
	world.ForEach<IrisTransitionComponent>([&](Entity entity,
		IrisTransitionComponent& component) {

		if (!component.enabled || !component.previewInEditMode) {
			return;
		}
		if (!world.IsAlive(previewEntity) ||
			previewSerial < component.runtimeEditPreviewSerial ||
			(previewSerial == component.runtimeEditPreviewSerial &&
				entity.index < previewEntity.index)) {

			previewEntity = entity;
			previewSerial = component.runtimeEditPreviewSerial;
		}
		});

	if (!world.IsAlive(previewEntity)) {
		ResetPlayback();
		editCommandPreview_ = false;
		editPreviewSerial_ = 0;
		ownerUUID_ = {};
		return false;
	}

	const auto& component = world.GetComponent<IrisTransitionComponent>(previewEntity);
	const UUID previewUUID = world.GetUUID(previewEntity);
	bool authoringPreviewRequested = editPreviewSerial_ != previewSerial;
	if (ownerUUID_ != previewUUID) {
		ownerUUID_ = previewUUID;
		editCommandPreview_ = false;
		authoringPreviewRequested = true;
	}
	if (authoringPreviewRequested) {
		editPreviewSerial_ = previewSerial;
		editCommandPreview_ = false;
	}
	if (editCommandPreview_ &&
		!IsSameProgress(component.previewProgress, editAuthoringProgress_)) {
		editCommandPreview_ = false;
	}
	if (authoringPreviewRequested ||
		(!IsPlaying() && !editCommandPreview_)) {

		CopySettings(component);
		progress_ = std::clamp(component.previewProgress, 0.0f, 1.0f);
		state_ = progress_ <= kProgressEpsilon ? IrisTransitionState::Open :
			1.0f - kProgressEpsilon <= progress_ ? IrisTransitionState::Covered :
			IrisTransitionState::Stopped;
	}
	return true;
}

void Engine::IrisTransitionSystem::RefreshOwnerSettings(ECSWorld& world) {

	if (!ownerUUID_) {
		return;
	}

	bool found = false;
	world.ForEach<IrisTransitionComponent>([&](Entity entity,
		IrisTransitionComponent& component) {

		if (!found && world.GetUUID(entity) == ownerUUID_) {
			CopySettings(component);
			found = true;
		}
		});
}

void Engine::IrisTransitionSystem::UpdatePlayback(float deltaTime) {

	if (!IsPlaying()) {
		return;
	}

	if (duration_ <= kProgressEpsilon) {
		progress_ = targetProgress_;
	} else {
		elapsed_ += (std::max)(deltaTime, 0.0f);
		const float time = std::clamp(elapsed_ / duration_, 0.0f, 1.0f);
		progress_ = std::lerp(startProgress_, targetProgress_,
			EasedValue(easing_, time));
		if (time < 1.0f) {
			return;
		}
	}

	progress_ = targetProgress_;
	state_ = targetProgress_ <= kProgressEpsilon ?
		IrisTransitionState::Open : IrisTransitionState::Covered;
}

void Engine::IrisTransitionSystem::UpdateRenderEntity(ECSWorld& world, bool visible) {

	auto* canvas = world.TryGetComponent<CanvasComponent>(renderEntity_);
	auto* sprite = world.TryGetComponent<SpriteRendererComponent>(renderEntity_);
	if (!canvas || !sprite) {
		return;
	}

	const Vector2 viewportSize = EngineContext::GetWindowSetting().gameSizeFloat;
	canvas->referenceResolution = viewportSize;
	sprite->size = viewportSize;
	sprite->visible = visible;
	sprite->material = BuiltinAssets::Materials::IrisTransition;
	sprite->parameterOverrides["transitionColor"].value = settings_.transitionColor;
	sprite->parameterOverrides["center"].value = settings_.screenPosition;
	sprite->parameterOverrides["progress"].value = std::clamp(progress_, 0.0f, 1.0f);
	sprite->parameterOverrides["edgeSoftness"].value = (std::max)(settings_.edgeSoftness, 0.0f);
	sprite->parameterOverrides["viewportSize"].value = viewportSize;
	sprite->parameterOverrides["invertMask"].value = settings_.invertMask ? 1.0f : 0.0f;
}

void Engine::IrisTransitionSystem::SyncComponentRuntime(ECSWorld& world) {

	world.ForEach<IrisTransitionComponent>([&](Entity,
		IrisTransitionComponent& component) {

		component.runtimeState = state_;
		component.runtimeProgress = progress_;
		});
}

void Engine::IrisTransitionSystem::CopySettings(
	const IrisTransitionComponent& component) {

	settings_.screenPosition = component.screenPosition;
	settings_.transitionColor = component.transitionColor;
	settings_.edgeSoftness = component.edgeSoftness;
	settings_.invertMask = component.invertMask;
	settings_.irisOutDuration = component.irisOutDuration;
	settings_.irisOutEasing = component.irisOutEasing;
	settings_.irisInDuration = component.irisInDuration;
	settings_.irisInEasing = component.irisInEasing;
	settings_.blockInput = component.blockInput;
	settings_.autoIrisInAfterSceneTransition =
		component.autoIrisInAfterSceneTransition;
	settings_.useUnscaledTime = component.useUnscaledTime;
}

void Engine::IrisTransitionSystem::StartPlayback(IrisTransitionState state,
	float target, float duration, EasingType easing) {

	target = std::clamp(target, 0.0f, 1.0f);
	startProgress_ = progress_;
	targetProgress_ = target;
	elapsed_ = 0.0f;
	duration_ = (std::max)(duration, 0.0f) *
		std::abs(targetProgress_ - startProgress_);
	easing_ = easing;
	state_ = state;
	if (IsSameProgress(startProgress_, targetProgress_)) {
		progress_ = targetProgress_;
		state_ = targetProgress_ <= kProgressEpsilon ?
			IrisTransitionState::Open : IrisTransitionState::Covered;
	}
}

void Engine::IrisTransitionSystem::ResetPlayback() {

	ownerUUID_ = {};
	state_ = IrisTransitionState::Open;
	progress_ = 0.0f;
	startProgress_ = 0.0f;
	targetProgress_ = 0.0f;
	elapsed_ = 0.0f;
	duration_ = 0.0f;
	easing_ = EasingType::Linear;
	inputBlocked_ = false;
	pendingSceneTransition_ = false;
}

bool Engine::IrisTransitionSystem::IsPlaying() const {

	return state_ == IrisTransitionState::IrisOut ||
		state_ == IrisTransitionState::IrisIn;
}

bool Engine::IrisTransitionSystem::IsTransitionActive() const {

	return IsPlaying() || state_ == IrisTransitionState::Covered;
}
