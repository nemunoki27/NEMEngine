#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/System/Interface/ISystem.h>

namespace Engine {

	//============================================================================
	//	TransformUpdateSystem class
	//	トランスフォームの更新、管理を行うシステム
	//============================================================================
	class TransformUpdateSystem :
		public ISystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		TransformUpdateSystem() = default;
		~TransformUpdateSystem() = default;

		void LateUpdate(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "TransformUpdateSystem"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// トランスフォーム更新のためのスタックノード
		struct StackNode {

			Entity entity = Entity::Null();
			bool parentDirty = false;
		};

		//--------- variables ----------------------------------------------------

		std::vector<Entity> roots_;
		std::vector<StackNode> stack_;
	};
} // Engine