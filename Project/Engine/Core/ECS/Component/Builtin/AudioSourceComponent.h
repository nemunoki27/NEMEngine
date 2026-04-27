#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Asset/AssetTypes.h>

// c++
#include <cstdint>
#include <string>

namespace Engine {

	//============================================================================
	//	AudioSourceComponent struct
	//============================================================================

	// Entityに音声再生設定を持たせるコンポーネント
	struct AudioSourceComponent {

		// 再生する音声アセット
		AssetID clip{};
		// コンポーネントが有効か
		bool enabled = true;
		// Play開始時に自動再生するか
		bool playOnAwake = true;
		// ループ再生するか
		bool loop = false;
		// 音量
		float volume = 1.0f;

		// Runtime用。Scene/Prefabには保存しない
		bool runtimePlaying = false;
		AssetID runtimeClip{};
		std::string runtimeKey{};
		uint64_t runtimeVoiceID = 0;
		bool runtimePlayOnAwakeConsumed = false;
	};

	// json変換
	void from_json(const nlohmann::json& in, AudioSourceComponent& component);
	void to_json(nlohmann::json& out, const AudioSourceComponent& component);

	ENGINE_REGISTER_COMPONENT(AudioSourceComponent, "AudioSource");
} // Engine
