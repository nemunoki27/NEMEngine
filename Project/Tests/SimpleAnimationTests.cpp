#include "SimpleAnimationTests.h"

#include <Engine/Core/Animation/Simple/SimpleAnimationPlayback.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Systems/Animation/SimpleAnimationSystem.h>
#include <Engine/Core/World/Systems/UI/UISelectableStyle.h>
#include <Engine/Core/World/Systems/UI/UISelectableAnimation.h>

// c++
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

	void Require(bool condition, const char* message) {

		if (!condition) throw std::runtime_error(message);
	}

	bool Near(float a, float b) {

		return std::abs(a - b) < 0.0001f;
	}
}

bool NEMTests::TestSimpleAnimation() {

	using namespace Engine;
	try {
		for (size_t type = 0; type < 8; ++type) {
			auto settings = MakeSimpleAnimationSettings(type);
			nlohmann::json json = settings;
			const auto restored = json.get<SimpleAnimationSettings>();
			Require(nlohmann::json(restored) == json, "設定の保存・再読込");
			for (double time : {0.0, 0.5, 1.0}) Require(EvaluateSimpleAnimation(restored, time).valid, "全型の補間");
		}
		auto scalar = MakeSimpleAnimationSettings(0);
		auto euler = MakeSimpleAnimationSettings(6);
		euler.rotationMode = SimpleAnimationRotationMode::Euler;
		euler.endEuler = Vector3(0, 720, 0);
		const auto quarterTurn = std::get<Quaternion>(EvaluateSimpleAnimation(euler, 0.125).value);
		const auto expectedTurn = Quaternion::FromEulerDegrees(Vector3(0, 90, 0));
		Require(Near(quarterTurn.y, expectedTurn.y) && Near(quarterTurn.w, expectedTurn.w), "オイラーの複数回転");
		const auto savedEuler = nlohmann::json(euler).get<SimpleAnimationSettings>();
		Require(savedEuler.endEuler.y == 720 && savedEuler.rotationMode == SimpleAnimationRotationMode::Euler, "回転数の保存");
		scalar.start = 10.0f; scalar.end = 20.0f;
		Require(Near(std::get<float>(EvaluateSimpleAnimation(scalar, 0.5).value), 15), "補間の中間値");
		scalar.delay = 2;
		Require(Near(std::get<float>(EvaluateSimpleAnimation(scalar, 1).value), 10), "開始待ち");
		scalar.duration = 0;
		Require(!EvaluateSimpleAnimation(scalar, 1).finished && EvaluateSimpleAnimation(scalar, 2).finished, "時間0の終端");
		scalar = MakeSimpleAnimationSettings(0);
		scalar.mode = SimpleAnimationMode::Sin;
		scalar.center = 2.0f; scalar.amplitude = 0.5f; scalar.period = 2;
		Require(Near(std::get<float>(EvaluateSimpleAnimation(scalar, 0.5).value), 2.5f), "Sinの周期と振幅");
		scalar.mode = SimpleAnimationMode::Cos;
		Require(Near(std::get<float>(EvaluateSimpleAnimation(scalar, 0).value), 2.5f), "Cosの開始値");
		scalar = MakeSimpleAnimationSettings(0);
		scalar.repeat = SimpleAnimationRepeat::PingPong;
		scalar.cycleCount = 1;
		Require(Near(std::get<float>(EvaluateSimpleAnimation(scalar, 1.5).value), 0.5f), "往復の復路");
		Require(EvaluateSimpleAnimation(scalar, 2).finished && Near(std::get<float>(EvaluateSimpleAnimation(scalar, 2).value), 0), "往復の終端");
		scalar.cycleCount = 0;
		Require(EvaluateSimpleAnimation(scalar, 20000.25).completedCycles == 10000, "大きな経過時間");
		auto integer = MakeSimpleAnimationSettings(7);
		integer.start = int32_t(-2); integer.end = int32_t(-1);
		Require(std::get<int32_t>(EvaluateSimpleAnimation(integer, 0.5).value) == -2, "負数の丸め");
		integer.start = INT32_MIN; integer.end = INT32_MAX;
		Require(std::get<int32_t>(EvaluateSimpleAnimation(integer, 1).value) == INT32_MAX, "整数の上限");
		auto rotation = MakeSimpleAnimationSettings(6);
		rotation.end = Quaternion::FromEulerDegrees(Vector3(0, 90, 0));
		Require(Near(std::get<Quaternion>(EvaluateSimpleAnimation(rotation, 0.5).value).Length(), 1), "回転の正規化");
		Require(!EvaluateSimpleAnimation(scalar, std::numeric_limits<double>::infinity()).valid, "非有限時刻の拒否");

		RegisterBuiltinAnimationProperties();
		ECSWorld world;
		const Entity object = world.CreateEntity();
		world.AddComponent<TransformComponent>(object);
		world.AddComponent<SimpleAnimationComponent>(object);
		auto& animation = world.GetComponent<SimpleAnimationComponent>(object);
		animation.settings = MakeSimpleAnimationSettings(2);
		animation.settings.end = Vector3(10, 0, 0);
		animation.autoApply = true;
		animation.binding = {"Transform", "localPos", AnimationValueType::Vector3};
		SystemContext context;
		context.mode = WorldMode::Play; context.deltaTime = context.unscaledDeltaTime = 0.25f;
		SimpleAnimationSystem system;
		system.Update(world, context); system.Update(world, context);
		Require(Near(world.GetComponent<TransformComponent>(object).localPos.x, 2.5f), "Transformへの適用");
		animation.runtimeRequest = 2;
		system.Update(world, context);
		Require(Near(animation.runtimeTime, 0.25f) && animation.runtimePaused, "一時停止");
		animation.runtimeRequest = 6; animation.runtimeSeekTime = 0.75f;
		system.Update(world, context);
		Require(Near(world.GetComponent<TransformComponent>(object).localPos.x, 7.5f) && animation.runtimePaused, "停止中のSeek");
		animation.runtimeRequest = 3;
		system.Update(world, context);
		Require(animation.runtimeFinished, "再開と完了");
		animation.runtimeRequest = 1; animation.settings.unscaledTime = true;
		context.deltaTime = 0;
		system.Update(world, context); system.Update(world, context);
		Require(Near(animation.runtimeTime, 0.25f), "非スケール時間");
		animation.settings.end = Vector3(20, 0, 0);
		system.Update(world, context);
		Require(Near(world.GetComponent<TransformComponent>(object).localPos.x, 10), "設定変更後の再評価");
		animation.runtimeRequest = 5;
		system.Update(world, context);
		Require(Near(world.GetComponent<TransformComponent>(object).localPos.x, 0), "Resetの復元");
		context.mode = WorldMode::Edit; animation.playInEditMode = true;
		animation.settings.unscaledTime = false;
		animation.runtimeRequest = 1;
		system.Update(world, context); system.Update(world, context);
		Require(Near(animation.runtimeTime, 0.25f), "編集時はゲーム時間が停止していても再生する");
		SimpleAnimationSystem::RestoreEditPreviews(world);
		Require(Near(world.GetComponent<TransformComponent>(object).localPos.x, 0), "保存前のプレビュー復元");
		const auto saved = nlohmann::json(animation);
		Require(!saved.contains("runtime") && !saved.contains("runtimeTime"), "再生状態を保存しない");

		world.AddComponent<UISelectableComponent>(object);
		auto& ui = world.GetComponent<UISelectableComponent>(object);
		auto& runtime = world.GetComponent<UISelectableRuntimeComponent>(object);
		UISelectableEntry entry{object, Entity::Null(), &ui, &runtime};
		ui.selected.scaleAnimation.end = Vector2(2, 2);
		runtime.state = UISelectableState::Selected;
		UISelectableStyle::UpdateSelectableVisual(world, entry, 0.1f, 0.1f);
		UISelectableStyle::UpdateSelectableVisual(world, entry, 0.1f, 0.1f);
		Require(Near(runtime.currentScale.x, 2), "UI状態のスケール");
		runtime.state = UISelectableState::Normal;
		UISelectableStyle::UpdateSelectableVisual(world, entry, 0, 0);
		Require(Near(runtime.currentScale.x, 2), "UI切替の連続性");
		ui.submitted.scaleAnimation.repeat = SimpleAnimationRepeat::PingPong;
		runtime.state = UISelectableState::Submitted;
		UISelectableStyle::UpdateSelectableVisual(world, entry, 0, 0);
		UISelectableStyle::UpdateSelectableVisual(world, entry, 0.2f, 0.2f);
		Require(UISelectableAnimation::IsSubmitTransitionFinished(ui, runtime), "決定の往復が一回で終了");
		std::cout << "SimpleAnimation: evaluation / playback / binding / preview / UI passed\n";
		return true;
	} catch (const std::exception& error) {
		std::cerr << "SimpleAnimation: " << error.what() << '\n';
		return false;
	}
}
