#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Playback/AnimationPlaybackTypes.h>

// c++
#include <optional>
#include <filesystem>
#include <utility>
#include <variant>

namespace Engine {

	enum class AnimationControllerParameterType : uint8_t {

		Float,
		Integer,
		Boolean,
		Trigger,
	};

	enum class AnimationControllerConditionMode : uint8_t {

		If,
		IfNot,
		Greater,
		Less,
		Equals,
		NotEqual,
	};

	using AnimationControllerParameterValue = std::variant<float, int32_t, bool>;

	struct AnimationControllerParameter {

		std::string name;
		AnimationControllerParameterType type = AnimationControllerParameterType::Float;
		AnimationControllerParameterValue defaultValue = 0.0f;
	};

	struct AnimationControllerCondition {

		std::string parameter;
		AnimationControllerConditionMode mode = AnimationControllerConditionMode::If;
		float threshold = 0.0f;
	};

	struct AnimationControllerTransition {

		// 空のfromはAny State
		std::string from;
		std::string to;
		std::vector<AnimationControllerCondition> conditions;
		float duration = 0.1f;
		bool hasExitTime = false;
		float exitTime = 1.0f;
		bool canTransitionToSelf = false;
	};

	struct AnimationControllerAsset {

		AssetID guid{};
		std::string name;
		std::string defaultState;
		std::vector<AnimationGroup> states;
		std::vector<AnimationControllerParameter> parameters;
		std::vector<AnimationControllerTransition> transitions;
	};

	struct AnimationControllerRuntime {

		std::string state;
		std::vector<std::pair<std::string, AnimationControllerParameterValue>> parameters;
		std::string evaluatedState;
		float previousNormalizedTime = -1.0f;
	};

	//============================================================================
	//	AnimationControllerEvaluator class
	//	条件の判定とTrigger消費を担当する
	//============================================================================
	class AnimationControllerEvaluator {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		static bool Validate(const AnimationControllerAsset& controller, std::string& error);
		static void Reset(const AnimationControllerAsset& controller, AnimationControllerRuntime& runtime);
		static bool SetParameter(const AnimationControllerAsset& controller, AnimationControllerRuntime& runtime,
			const std::string& name, const AnimationControllerParameterValue& value);
		static const AnimationControllerParameterValue* GetParameter(const AnimationControllerRuntime& runtime, const std::string& name);
		static std::optional<size_t> Evaluate(const AnimationControllerAsset& controller,
			AnimationControllerRuntime& runtime, float normalizedTime);
	};

	void to_json(nlohmann::json& out, const AnimationControllerAsset& controller);
	void from_json(const nlohmann::json& in, AnimationControllerAsset& controller);
	bool LoadAnimationControllerAsset(const std::filesystem::path& path, AnimationControllerAsset& controller);
	bool SaveAnimationControllerAsset(const std::filesystem::path& path, const AnimationControllerAsset& controller);
}
