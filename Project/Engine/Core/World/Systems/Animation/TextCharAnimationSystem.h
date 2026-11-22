#pragma once

#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

namespace Engine {

	class AssetDatabase;
	class AnimationClipManager;
	struct TextCharAnimationComponent;
	bool ResolveLegacyTextCharClip(ECSWorld& world, AssetDatabase& database, AnimationClipManager& clips,
		TextCharAnimationComponent& component);

	// 文章の再生時計から各文字のローカル時刻を決める
	class TextCharAnimationSystem final : public ISystem {
	public:
		void Update(ECSWorld& world, SystemContext& context) override;
		const char* GetName() const override { return "TextCharAnimationSystem"; }
	};
}
