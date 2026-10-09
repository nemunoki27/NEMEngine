#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>

namespace Assimp { class Importer; }
struct aiScene;

namespace Engine::FBXScenePreparation {

	// 静的FBXの部品配置を頂点へ反映し骨付きモデルは保持する
	const aiScene* Prepare(Assimp::Importer& importer, const aiScene* scene, const std::filesystem::path& path);
}
