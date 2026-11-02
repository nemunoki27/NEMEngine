#include "UISelectableAnimation.h"

#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/World/Systems/Audio/AudioWorldVoiceStorage.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>

namespace Engine::UISelectableAnimation {
	void PlayStateSound(Engine::ECSWorld& world, Engine::Entity entity,
		const Engine::UITransitionStyle& style, Engine::SystemContext& context) {

		if (!style.sound || !context.assetDatabase) {
			return;
		}
		const std::filesystem::path fullPath = context.assetDatabase->ResolveFullPath(style.sound);
		if (fullPath.empty()) {
			return;
		}

		Engine::Audio* audio = Engine::Audio::GetInstance();
		if (audio->EnsureLoaded(fullPath)) {
			uint64_t voiceID = audio->PlayManaged(Engine::Algorithm::PathToUTF8(fullPath), false,
				std::clamp(style.soundVolume, 0.0f, 1.0f));
			world.GetStorage().Get<AudioWorldVoiceStorage>().Add(entity, voiceID);
		}
	}

	bool IsSubmitTransitionFinished(const UISelectableComponent& selectable, const UISelectableRuntimeComponent& runtime) {

		return !selectable.submitted.animationEnabled || (runtime.colorPlayback.sample.finished && runtime.scalePlayback.sample.finished);
	}
}
