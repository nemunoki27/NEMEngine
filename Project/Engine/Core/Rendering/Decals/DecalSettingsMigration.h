#pragma once

#include <Engine/Core/World/ECS/Entity/Entity.h>

namespace Engine {

	class AssetDatabase;
	class ECSWorld;

	// 旧投影設定を同じオブジェクトの個別MaterialとUVTransformへ移す
	void MigrateDecalSettings(ECSWorld& world, Entity entity, AssetDatabase* database);
}
