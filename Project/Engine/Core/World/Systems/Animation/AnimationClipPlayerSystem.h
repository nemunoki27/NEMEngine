#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/World/Components/Animation/AnimationClipPlayerComponent.h>
#include "AnimationPlaybackTime.h"

// c++
#include <unordered_map>

namespace Engine {

	class AnimationClipPlayerSystem : public ISystem {
	public:
		void Update(ECSWorld& world, SystemContext& context) override;
		void OnWorldExit(ECSWorld& world, SystemContext& context) override;
		static void RestoreEditPreviews(ECSWorld& world);
		const char* GetName() const override { return "AnimationClipPlayerSystem"; }

	private:
		struct PlaybackState {
			AssetID clip{};
			uint64_t revision = 0;
			AnimationClipRuntime playback;
			std::vector<AnimationPreviewBaseValue> base;
			bool initialized = false;
			bool edit = false;
			bool visited = false;
		};
		struct PlaybackStorage {
			std::unordered_map<UUID, PlaybackState> values;
		};
		void UpdatePlayer(ECSWorld& world, Entity entity, AnimationClipPlayerComponent& player, SystemContext& context);
	};
}
