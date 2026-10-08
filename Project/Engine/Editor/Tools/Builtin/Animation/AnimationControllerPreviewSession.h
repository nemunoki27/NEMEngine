#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Systems/Animation/AnimationPlayerSystem.h>
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>

namespace Engine {

	class ECSWorldLifetime;

	//============================================================================
	//	AnimationControllerPreviewSession class
	//	未保存Controllerの再生と開始Worldへの復元を所有する
	//============================================================================
	class AnimationControllerPreviewSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		~AnimationControllerPreviewSession();
		bool Begin(ECSWorld& world, Entity entity, const AnimationControllerAsset& definition);
		void Update(ECSWorld& world, SystemContext& context);
		void End();
		bool SetParameter(const std::string& name, const AnimationControllerParameterValue& value);

		//--------- accessor -----------------------------------------------------

		bool IsActive() const { return world_ != nullptr; }
		const AnimationControllerRuntime& GetRuntime() const { return runtime_; }
		const std::string& GetState() const { return player_.runtimeCurrentGroup; }
	private:
		//--------- variables ----------------------------------------------------

		ECSWorld* world_ = nullptr;
		std::weak_ptr<const ECSWorldLifetime> lifetime_;
		Entity entity_{};
		AnimationControllerAsset definition_;
		AnimationControllerRuntime runtime_;
		AnimationPlayerComponent player_;
		AnimationPlayerSystem system_;
	};
}
