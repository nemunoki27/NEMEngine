#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	JointAttachmentComponent struct
	//============================================================================
	// エンティティをスキンメッシュのジョイントへ親子付けするための情報
	struct JointAttachmentComponent {

		// 親にするスキンメッシュエンティティのシーンローカルID
		UUID skinnedEntityLocalFileID{};
		// 親にするジョイント名
		std::string jointName{};
	};

	// json変換
	void from_json(const nlohmann::json& in, JointAttachmentComponent& component);
	void to_json(nlohmann::json& out, const JointAttachmentComponent& component);

	ENGINE_REGISTER_COMPONENT(JointAttachmentComponent, "JointAttachment");
} // Engine
