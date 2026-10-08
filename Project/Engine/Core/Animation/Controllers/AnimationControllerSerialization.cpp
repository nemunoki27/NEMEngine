#include "AnimationControllerAsset.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <stdexcept>

void Engine::to_json(nlohmann::json& out, const AnimationControllerAsset& controller) {

	out = { { "guid", ToString(controller.guid) }, { "name", controller.name }, { "defaultState", controller.defaultState },
		{ "states", nlohmann::json::array() }, { "parameters", nlohmann::json::array() }, { "transitions", nlohmann::json::array() } };
	// Clip設定は直接再生と同じ形式で保存する
	for (const auto& state : controller.states) out["states"].push_back(SaveAnimationGroup(state));
	for (const auto& parameter : controller.parameters) {

		const auto value = std::visit([](auto input) { return nlohmann::json(input); }, parameter.defaultValue);
		out["parameters"].push_back({ { "name", parameter.name },
			{ "type", EnumAdapter<AnimationControllerParameterType>::ToString(parameter.type) }, { "defaultValue", value } });
	}
	for (const auto& transition : controller.transitions) {

		nlohmann::json conditions = nlohmann::json::array();
		for (const auto& condition : transition.conditions) {
			conditions.push_back({ { "parameter", condition.parameter },
				{ "mode", EnumAdapter<AnimationControllerConditionMode>::ToString(condition.mode) }, { "threshold", condition.threshold } });
		}
		out["transitions"].push_back({ { "from", transition.from }, { "to", transition.to }, { "conditions", std::move(conditions) },
			{ "duration", transition.duration }, { "hasExitTime", transition.hasExitTime }, { "exitTime", transition.exitTime },
			{ "canTransitionToSelf", transition.canTransitionToSelf } });
	}
}

void Engine::from_json(const nlohmann::json& in, AnimationControllerAsset& controller) {

	AnimationControllerAsset loaded;
	loaded.guid = ParseAssetID(in, "guid");
	loaded.name = in.value("name", std::string());
	loaded.defaultState = in.value("defaultState", std::string());
	// 読込完了までは呼出し元の定義を変更しない
	for (const auto& state : in.at("states")) loaded.states.push_back(LoadAnimationGroup(state));
	for (const auto& item : in.at("parameters")) {

		AnimationControllerParameter parameter;
		parameter.name = item.at("name").get<std::string>();
		const auto type = EnumAdapter<AnimationControllerParameterType>::FromString(item.at("type").get<std::string>());
		if (!type) throw std::invalid_argument("Animation ControllerのParameter型が不正です");
		parameter.type = *type;
		switch (parameter.type) {
		case AnimationControllerParameterType::Float: parameter.defaultValue = item.value("defaultValue", 0.0f); break;
		case AnimationControllerParameterType::Integer: parameter.defaultValue = item.value("defaultValue", int32_t(0)); break;
		case AnimationControllerParameterType::Boolean:
		case AnimationControllerParameterType::Trigger: parameter.defaultValue = item.value("defaultValue", false); break;
		}
		loaded.parameters.push_back(std::move(parameter));
	}
	for (const auto& item : in.at("transitions")) {

		AnimationControllerTransition transition;
		transition.from = item.value("from", std::string());
		transition.to = item.at("to").get<std::string>();
		transition.duration = item.value("duration", 0.1f);
		transition.hasExitTime = item.value("hasExitTime", false);
		transition.exitTime = item.value("exitTime", 1.0f);
		transition.canTransitionToSelf = item.value("canTransitionToSelf", false);
		for (const auto& conditionJSON : item.at("conditions")) {

			AnimationControllerCondition condition;
			condition.parameter = conditionJSON.at("parameter").get<std::string>();
			const auto mode = EnumAdapter<AnimationControllerConditionMode>::FromString(conditionJSON.at("mode").get<std::string>());
			if (!mode) throw std::invalid_argument("Animation Controllerの遷移条件が不正です");
			condition.mode = *mode;
			condition.threshold = conditionJSON.value("threshold", 0.0f);
			transition.conditions.push_back(std::move(condition));
		}
		loaded.transitions.push_back(std::move(transition));
	}
	std::string error;
	if (!AnimationControllerEvaluator::Validate(loaded, error)) throw std::invalid_argument(error);
	controller = std::move(loaded);
}

bool Engine::LoadAnimationControllerAsset(const std::filesystem::path& path, AnimationControllerAsset& controller) {

	nlohmann::json data;
	std::string diagnostic;
	if (!JsonFile::TryLoad(path, data, &diagnostic)) {
		Logger::Output(LogType::Engine, spdlog::level::err, "{}", diagnostic);
		return false;
	}
	try {
		// 検証済みの定義だけを公開する
		AnimationControllerAsset loaded = data.get<AnimationControllerAsset>();
		controller = std::move(loaded);
		return true;
	} catch (const std::exception& error) {
		Logger::Output(LogType::Engine, spdlog::level::err, "AnimationControllerの読み込みに失敗しました path={} 内容={}",
			Algorithm::PathToUTF8(path), error.what());
		return false;
	}
}

bool Engine::SaveAnimationControllerAsset(const std::filesystem::path& path, const AnimationControllerAsset& controller) {

	std::string error;
	if (!AnimationControllerEvaluator::Validate(controller, error)) {
		Logger::Output(LogType::Engine, spdlog::level::err, "AnimationControllerの保存に失敗しました 内容={}", error);
		return false;
	}
	// 完全なJSONを書き終えてから元ファイルと置き換える
	return JsonFile::SaveCanonical(path, nlohmann::json(controller), 2);
}
