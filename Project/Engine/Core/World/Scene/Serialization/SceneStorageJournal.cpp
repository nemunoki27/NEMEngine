#include "SceneStorageJournal.h"

//============================================================================
//	include
//============================================================================
#include "SceneStorageFiles.h"
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

namespace {

	Engine::JsonFileJournal::Scope MakeSceneScope() {

		// Sceneの書込範囲と既存の復旧記録先を維持する
		return { Engine::RuntimePaths::GetSavedRoot() / "SceneAssetRecovery", Engine::SceneStorageFiles::IsWritable };
	}
}

bool Engine::SceneStorageJournal::Commit(const std::vector<SceneStorageChange>& changes, const std::string& label,
	std::string& error, const RecoveryAction& recover) {

	return JsonFileJournal::Commit(MakeSceneScope(), changes, label, error, recover);
}

std::vector<std::filesystem::path> Engine::SceneStorageJournal::GetRecoveries(bool unfinishedOnly) {

	return JsonFileJournal::GetRecoveries(MakeSceneScope(), unfinishedOnly);
}

bool Engine::SceneStorageJournal::Recover(const std::filesystem::path& directory, std::string& error, const RecoveryCheck& check) {

	return JsonFileJournal::Recover(MakeSceneScope(), directory, error, check);
}
