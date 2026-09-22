#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Meshes/Animation/SkinnedMeshAnimationManager.h>

namespace Engine {

	//============================================================================
	//	SkinnedAnimationSystem class
	//	骨アニメーションデータの更新を行うシステム
	//============================================================================
	class SkinnedAnimationSystem :
		public ISystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SkinnedAnimationSystem() = default;
		~SkinnedAnimationSystem() = default;

		void LateUpdate(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "SkinnedAnimationSystem"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		// 初期クリップの名前を解決
		std::string ResolveInitialClip(const SkinnedMeshAnimationSet& animationSet, const std::string& requestedClip);
	};
} // Engine
