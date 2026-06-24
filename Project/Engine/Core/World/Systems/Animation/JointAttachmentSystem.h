#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	JointAttachmentSystem class
	//	ジョイントへ親子付けされたエンティティを、ジョイントのワールド行列へ追従させるシステム
	//	スケルトン更新の後に動かす必要がある、更新順序に注意
	//============================================================================
	class JointAttachmentSystem :
		public ISystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		JointAttachmentSystem() = default;
		~JointAttachmentSystem() = default;

		void LateUpdate(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "JointAttachmentSystem"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// サブツリー更新のためのスタック
		std::vector<Entity> stack_;
	};
} // Engine
