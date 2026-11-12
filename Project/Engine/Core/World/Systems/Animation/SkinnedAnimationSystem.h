#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

// c++
#include <string>

namespace Engine {

	struct SkinnedMeshAnimationSet;

	//============================================================================
	//	SkinnedAnimationSystem class
	//	骨アニメーションデータの更新を行うシステム
	//============================================================================
	class SkinnedAnimationSystem : public ISystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SkinnedAnimationSystem() = default;
		~SkinnedAnimationSystem() = default;

		// 再生時刻と骨格の描画用パレットを更新する
		void LateUpdate(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		// システムの表示名を取得する
		const char* GetName() const override { return "SkinnedAnimationSystem"; }

	};
} // Engine
