#include "AnimationControllerAsset.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace {

	using namespace Engine;

	const AnimationControllerParameter* FindParameter(const AnimationControllerAsset& controller, const std::string& name) {

		const auto found = std::find_if(controller.parameters.begin(), controller.parameters.end(), [&](const auto& parameter) {
			return parameter.name == name;
		});
		return found != controller.parameters.end() ? &*found : nullptr;
	}

	bool IsParameterValueValid(AnimationControllerParameterType type, const AnimationControllerParameterValue& value) {

		switch (type) {
		case AnimationControllerParameterType::Float:
			return std::holds_alternative<float>(value) && std::isfinite(std::get<float>(value));
		case AnimationControllerParameterType::Integer: return std::holds_alternative<int32_t>(value);
		case AnimationControllerParameterType::Boolean:
		case AnimationControllerParameterType::Trigger: return std::holds_alternative<bool>(value);
		}
		return false;
	}

	bool Matches(const AnimationControllerCondition& condition, const AnimationControllerParameterValue& value) {

		if (const bool* boolean = std::get_if<bool>(&value)) {
			return condition.mode == AnimationControllerConditionMode::If ? *boolean :
				condition.mode == AnimationControllerConditionMode::IfNot && !*boolean;
		}
		const double number = std::visit([](auto input) { return static_cast<double>(input); }, value);
		switch (condition.mode) {
		case AnimationControllerConditionMode::Greater: return number > condition.threshold;
		case AnimationControllerConditionMode::Less: return number < condition.threshold;
		case AnimationControllerConditionMode::Equals: return number == condition.threshold;
		case AnimationControllerConditionMode::NotEqual: return number != condition.threshold;
		default: return false;
		}
	}
}

bool Engine::AnimationControllerEvaluator::Validate(const AnimationControllerAsset& controller, std::string& error) {

	error.clear();
	std::unordered_set<std::string> states;
	for (const auto& state : controller.states) {
		if (state.name.empty() || !states.emplace(state.name).second) {
			error = "状態名が空か重複しています";
			return false;
		}
		std::unordered_set<std::string> clips;
		for (const auto& clip : state.states) {

			if (clip.name.empty() || !clips.emplace(clip.name).second || !clip.clip || !std::isfinite(clip.speed) ||
				!std::isfinite(clip.weight) || clip.weight < 0.0f || !std::isfinite(clip.startDelay) || clip.startDelay < 0.0f ||
				!std::isfinite(clip.interval) || clip.interval < 0.0f || clip.loopCount < 0 || clip.pingPongCount < 0 ||
				!std::isfinite(clip.loopBridge.duration) || clip.loopBridge.duration < 0.0f) {

				error = "Clipの名前、参照または再生設定が不正です";
				return false;
			}
		}
	}
	if (!states.contains(controller.defaultState)) {
		error = "開始状態が見つかりません";
		return false;
	}
	std::unordered_set<std::string> names;
	for (const auto& parameter : controller.parameters) {
		if (parameter.name.empty() || !names.emplace(parameter.name).second || !IsParameterValueValid(parameter.type, parameter.defaultValue)) {
			error = "Parameterの名前または型が不正です";
			return false;
		}
	}
	for (const auto& transition : controller.transitions) {

		if ((!transition.from.empty() && !states.contains(transition.from)) || !states.contains(transition.to) ||
			!std::isfinite(transition.duration) || transition.duration < 0.0f || !std::isfinite(transition.exitTime) || transition.exitTime < 0.0f) {
			error = "遷移先または遷移時間が不正です";
			return false;
		}
		for (const auto& condition : transition.conditions) {

			const auto* parameter = FindParameter(controller, condition.parameter);
			if (!parameter || !std::isfinite(condition.threshold)) {
				error = "遷移条件のParameterが見つからないか値が不正です";
				return false;
			}
			const bool boolean = parameter->type == AnimationControllerParameterType::Boolean || parameter->type == AnimationControllerParameterType::Trigger;
			const bool booleanMode = condition.mode == AnimationControllerConditionMode::If || condition.mode == AnimationControllerConditionMode::IfNot;
			if (boolean != booleanMode) {
				error = "遷移条件とParameterの型が一致しません";
				return false;
			}
		}
	}
	return true;
}

void Engine::AnimationControllerEvaluator::Reset(const AnimationControllerAsset& controller, AnimationControllerRuntime& runtime) {

	runtime.state = controller.defaultState;
	runtime.parameters.clear();
	runtime.evaluatedState.clear();
	runtime.previousNormalizedTime = -1.0f;
	// Triggerは起動時に発火させない
	for (const auto& parameter : controller.parameters) {
		runtime.parameters.emplace_back(parameter.name, parameter.type == AnimationControllerParameterType::Trigger ?
			AnimationControllerParameterValue(false) : parameter.defaultValue);
	}
}

bool Engine::AnimationControllerEvaluator::SetParameter(const AnimationControllerAsset& controller, AnimationControllerRuntime& runtime,
	const std::string& name, const AnimationControllerParameterValue& value) {

	const auto* parameter = FindParameter(controller, name);
	if (!parameter || !IsParameterValueValid(parameter->type, value)) return false;
	const auto found = std::find_if(runtime.parameters.begin(), runtime.parameters.end(), [&](const auto& parameterValue) {
		return parameterValue.first == name;
	});
	if (found == runtime.parameters.end()) runtime.parameters.emplace_back(name, value);
	else found->second = value;
	return true;
}

const Engine::AnimationControllerParameterValue* Engine::AnimationControllerEvaluator::GetParameter(
	const AnimationControllerRuntime& runtime, const std::string& name) {

	const auto found = std::find_if(runtime.parameters.begin(), runtime.parameters.end(), [&](const auto& parameter) {
		return parameter.first == name;
	});
	return found != runtime.parameters.end() ? &found->second : nullptr;
}

std::optional<size_t> Engine::AnimationControllerEvaluator::Evaluate(const AnimationControllerAsset& controller,
	AnimationControllerRuntime& runtime, float normalizedTime) {

	if (!std::isfinite(normalizedTime)) return std::nullopt;
	// 状態が変わったら終了時刻の判定時計を戻す
	const float previousTime = runtime.evaluatedState == runtime.state ? runtime.previousNormalizedTime : -1.0f;
	runtime.evaluatedState = runtime.state;
	runtime.previousNormalizedTime = normalizedTime;
	// Any Stateを先に、同じ種類では一覧順に判定する
	for (int pass = 0; pass < 2; ++pass) {
		for (size_t index = 0; index < controller.transitions.size(); ++index) {

			const auto& transition = controller.transitions[index];
			if (transition.from.empty() != (pass == 0) ||
				(!transition.from.empty() && transition.from != runtime.state) ||
				(!transition.canTransitionToSelf && transition.to == runtime.state)) continue;
			if (transition.hasExitTime) {

				// 1未満は毎周回、1以上は一度だけ通過を判定する
				float exitTime = transition.exitTime;
				if (exitTime < 1.0f && previousTime >= exitTime) {
					exitTime += std::floor(previousTime - exitTime) + 1.0f;
				}
				if (previousTime >= exitTime || normalizedTime < exitTime) continue;
			}
			const bool matched = std::all_of(transition.conditions.begin(), transition.conditions.end(), [&](const auto& condition) {

				const auto* parameter = GetParameter(runtime, condition.parameter);
				return parameter && Matches(condition, *parameter);
			});
			if (!matched) continue;
			// 成立した遷移が使うTriggerだけを消費する
			for (const auto& condition : transition.conditions) {
				const auto* parameter = FindParameter(controller, condition.parameter);
				if (parameter && parameter->type == AnimationControllerParameterType::Trigger) SetParameter(controller, runtime, parameter->name, false);
			}
			runtime.state = transition.to;
			runtime.evaluatedState.clear();
			return index;
		}
	}
	return std::nullopt;
}
