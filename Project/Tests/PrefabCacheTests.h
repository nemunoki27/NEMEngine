#pragma once

#include <Engine/Core/Assets/AssetTypes.h>

namespace Engine {
	class AssetDatabase;
}

// 検証用Prefabの更新と基準データの寿命を確認する
bool TestPrefabCacheLifetime(Engine::AssetDatabase& database, Engine::AssetID asset);
