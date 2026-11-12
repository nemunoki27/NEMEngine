#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Lighting/LightInfluenceSettings.h>

namespace Engine {

	// Bindingで指定されたライトの共通設定を取得する
	LightInfluenceSettings* ResolveLightInfluenceSettings(ECSWorld& world, Entity entity, int32_t type);
}
