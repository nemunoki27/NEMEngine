#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>

// c++
#include <vector>
#include <cstdint>

namespace Engine {

	class ECSWorld;

	// Worldで再生するUIのOneShotを所有する
	class AudioWorldVoiceStorage {
	public:
		~AudioWorldVoiceStorage();
		AudioWorldVoiceStorage() = default;
		AudioWorldVoiceStorage(const AudioWorldVoiceStorage&) = delete;
		AudioWorldVoiceStorage& operator=(const AudioWorldVoiceStorage&) = delete;

		// 再生Voiceを所有Entityへ接続する
		void Add(Entity entity, uint64_t voiceID);
		// 終了したVoiceと削除・非アクティブのEntityを回収する
		void Update(ECSWorld& world);
		// World終了前にすべて停止する
		void Clear();
	private:
		struct Entry {
			Entity entity;
			uint64_t voiceID = 0;
		};
		std::vector<Entry> voices_;
	};
}
