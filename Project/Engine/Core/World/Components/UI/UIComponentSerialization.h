#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/UUID.h>

#include <json.hpp>

namespace Engine::UIComponentSerialization {

	// エンティティ参照を読み込む
	UUID ReadEntityReference(const nlohmann::json& value);

	// シーンまたはプレファブ内のエンティティ参照を読み込む
	UUID ReadEntityReference(const nlohmann::json& in, const char* key);

	// プレファブ参照リマップが扱える形式でエンティティ参照を書き込む
	nlohmann::json WriteEntityReference(UUID localFileID);
}
