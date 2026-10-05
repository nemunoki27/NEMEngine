#include "AnimationControllerPreviewSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>

Engine::AnimationControllerPreviewSession::~AnimationControllerPreviewSession() {

	End();
}

bool Engine::AnimationControllerPreviewSession::Begin(ECSWorld& world, Entity entity,
	const AnimationControllerAsset& definition) {

	std::string error;
	if (!world.IsAlive(entity) || !AnimationControllerEvaluator::Validate(definition, error)) return false;
	End();
	// Componentを追加せず、Tool専用の再生状態を作る
	definition_ = definition;
	AnimationControllerEvaluator::Reset(definition_, runtime_);
	player_ = {};
	player_.groups = definition_.states;
	player_.defaultGroup = definition_.defaultState;
	player_.playInEditMode = true;
	world_ = &world;
	lifetime_ = world.GetLifetime();
	entity_ = entity;
	return true;
}

void Engine::AnimationControllerPreviewSession::Update(ECSWorld& world, SystemContext& context) {

	const auto lifetime = lifetime_.lock();
	if (!world_ || !lifetime || !lifetime->IsAlive() || world_ != &world || !world.IsAlive(entity_)) {

		End();
		return;
	}
	// Edit評価ではScriptのAnimationEventを配送しない
	SystemContext previewContext = context;
	previewContext.mode = WorldMode::Edit;
	system_.UpdatePlayer(world, entity_, player_, previewContext);
	if (player_.runtimeInTransition || player_.runtimeCurrentGroup.empty()) return;
	runtime_.state = player_.runtimeCurrentGroup;
	const auto transition = AnimationControllerEvaluator::Evaluate(definition_, runtime_, player_.runtimeNormalizedTimeValue);
	if (transition) {

		player_.runtimePlayRequest = definition_.transitions[*transition].to;
		player_.runtimePlayFade = definition_.transitions[*transition].duration;
	}
}

void Engine::AnimationControllerPreviewSession::End() {

	const auto lifetime = lifetime_.lock();
	if (world_ && lifetime && lifetime->IsAlive()) {

		// 開始Worldが残っている間に編集値へ戻す
		SystemContext context;
		system_.OnWorldExit(*world_, context);
	}
	world_ = nullptr;
	lifetime_.reset();
	entity_ = {};
	player_ = {};
	runtime_ = {};
	definition_ = {};
	system_ = {};
}

bool Engine::AnimationControllerPreviewSession::SetParameter(const std::string& name,
	const AnimationControllerParameterValue& value) {

	return IsActive() && AnimationControllerEvaluator::SetParameter(definition_, runtime_, name, value);
}
