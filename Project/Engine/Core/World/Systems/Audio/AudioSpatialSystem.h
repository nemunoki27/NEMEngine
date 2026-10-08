#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

namespace Engine {

	// Transform更新後に受音位置と定位を反映する
	class AudioSpatialSystem : public ISystem {
	public:
		void OnWorldExit(ECSWorld& world, SystemContext& context) override;
		void LateUpdate(ECSWorld& world, SystemContext& context) override;
		const char* GetName() const override { return "AudioSpatialSystem"; }
	private:
		bool multipleListeners_ = false;
		bool missingListener_ = false;
	};
}
