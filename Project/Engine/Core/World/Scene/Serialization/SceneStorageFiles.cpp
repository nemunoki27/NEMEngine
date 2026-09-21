#include "SceneStorageFiles.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

namespace Engine::SceneStorageFiles {

	std::string PathKey(const Path& path) {

		return Algorithm::ToLower(Algorithm::PathToUTF8(std::filesystem::weakly_canonical(path)));
	}

	bool IsInside(const Path& path, const Path& root) {

		if (path.empty() || root.empty()) return false;
		const std::string key = PathKey(path);
		const std::string parent = PathKey(root);
		return key.size() > parent.size() && key.starts_with(parent) &&
			(key[parent.size()] == '/' || key[parent.size()] == '\\');
	}

	bool IsWritable(const Path& path) {

		return IsInside(path, RuntimePaths::GetGameAssetsRoot()) || IsInside(path, RuntimePaths::GetEngineAssetsRoot());
	}

	std::string FileRevision(const Path& path) {

		if (!std::filesystem::exists(path)) return "missing";
		const std::string hash = ContentHash::FileSHA256(path);
		if (hash.empty()) throw std::runtime_error("ファイルを読み込めません: " + Algorithm::PathToUTF8(path));
		return hash;
	}
}
