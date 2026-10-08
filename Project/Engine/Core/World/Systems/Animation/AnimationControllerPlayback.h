#pragma once

namespace Engine {

	struct AnimationPlayerComponent;
	struct SystemContext;

	// Controllerの定義同期をSystemとNative APIで共有する
	namespace AnimationControllerPlayback {

		bool Synchronize(AnimationPlayerComponent& player, const SystemContext& context);
	}
}
