#include "SceneStorageFiles.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

namespace Engine::SceneStorageFiles {

	std::string PathKey(const Path& path) {

		return StorageFileUtility::PathKey(path);
	}

	bool IsInside(const Path& path, const Path& root) {

		return StorageFileUtility::IsInside(path, root);
	}

	bool IsWritable(const Path& path) {

		return IsInside(path, RuntimePaths::GetGameAssetsRoot()) || IsInside(path, RuntimePaths::GetEngineAssetsRoot());
	}

	std::string FileRevision(const Path& path) {

		return StorageFileUtility::FileRevision(path);
	}
}
