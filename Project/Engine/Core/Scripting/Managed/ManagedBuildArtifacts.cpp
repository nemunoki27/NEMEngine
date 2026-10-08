#include "ManagedBuildArtifacts.h"
#include "ManagedBuildUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

#include <algorithm>
#include <cwctype>

using namespace Engine::ManagedBuildUtility;

namespace {

	constexpr const wchar_t* kAssemblyFileName = L"GameScripts.dll";

}

Engine::ManagedBuildArtifacts::ManagedBuildArtifacts(ManagedScriptRuntime*& runtime) : runtime_(runtime) {}

bool Engine::ManagedBuildArtifacts::ValidateArtifacts(const std::filesystem::path& directory) const {

	// required: GameScripts.dll
	std::error_code existsError{};
	const std::filesystem::path dll = directory / kAssemblyFileName;
	if (!std::filesystem::exists(dll, existsError) || existsError) {
		return false;
	}

	// 任意のpdbとdeps.jsonとruntimeconfig.jsonで欠落は警告に留める
	const std::filesystem::path optional[] = {
		directory / L"GameScripts.pdb",
		directory / L"GameScripts.deps.json",
		directory / L"GameScripts.runtimeconfig.json",
	};
	for (const std::filesystem::path& artifact : optional) {
		std::error_code optionalError{};
		if (!std::filesystem::exists(artifact, optionalError) || optionalError) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"ManagedScriptBuildService: 任意成果物がありません path={}", ToUtf8Path(artifact));
		}
	}
	return true;
}

bool Engine::ManagedBuildArtifacts::CopyArtifacts(const std::filesystem::path& from, const std::filesystem::path& to) const {

	// ビルド出力ディレクトリの内容を丸ごとシャドウへコピーする、dll/pdb/deps/runtimeconfigと依存DLLを含む
	std::error_code copyError{};
	std::filesystem::create_directories(to, copyError);
	std::filesystem::copy(from, to,
		std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive,
		copyError);
	if (copyError) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: 成果物のCopyに失敗しました source={} destination={} 内容={}",
			ToUtf8Path(from), ToUtf8Path(to), copyError.message());
		return false;
	}
	return true;
}

void Engine::ManagedBuildArtifacts::UpdateLastKnownGood(const std::filesystem::path& shadowDirectory) {

	// LKG更新はtransaction化しincomingへcopy validateしてbackup退避と置換、失敗時rollbackで直前のusable LKGを失わない
	const std::filesystem::path lkg = LastKnownGoodDirectory();
	std::filesystem::path incoming = lkg;
	incoming += L".incoming";
	std::filesystem::path backup = lkg;
	backup += L".old";
	std::error_code ec{};

	// 1.incomingを空にしてシャドウをコピー
	std::filesystem::remove_all(incoming, ec);
	if (!CopyArtifacts(shadowDirectory, incoming)) {
		std::filesystem::remove_all(incoming, ec);
		lastKnownGoodUpdateFailed_ = true;
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptBuildService: Shadowを新しいLKGへCopyできないため以前のLKGを維持します");
		return;
	}

	// 2.必須の出力DLL等を検証してから置換する
	if (!ValidateArtifacts(incoming)) {
		std::filesystem::remove_all(incoming, ec);
		lastKnownGoodUpdateFailed_ = true;
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptBuildService: 新しいLKGの検証に失敗したため以前のLKGを維持します");
		return;
	}

	// 3.既存LKGをbackupへ退避し、Windowsでdirectory置換が失敗しても元を失わないようにする
	const bool lkgExists = std::filesystem::exists(lkg, ec);
	if (lkgExists) {
		std::filesystem::remove_all(backup, ec);
		std::filesystem::rename(lkg, backup, ec);
		if (ec) {
			std::filesystem::remove_all(incoming, ec);
			lastKnownGoodUpdateFailed_ = true;
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"ManagedScriptBuildService: 既存LKGのBackupに失敗したため以前のLKGを維持します 内容={}",
				ec.message());
			return;
		}
	}

	// 4. incomingをLKGへ移動し、失敗したらbackupからrollbackする
	std::filesystem::rename(incoming, lkg, ec);
	if (ec) {
		std::error_code rollbackEc{};
		if (lkgExists) {
			std::filesystem::rename(backup, lkg, rollbackEc);
		}
		std::filesystem::remove_all(incoming, rollbackEc);
		lastKnownGoodUpdateFailed_ = true;
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptBuildService: 新しいLKGの導入に失敗したため以前のLKGへ戻しました 内容={}", ec.message());
		return;
	}

	// 5.置換成功、backupのcleanup失敗はwarningに留める、LKGは既に正
	lastKnownGoodUpdateFailed_ = false;
	if (lkgExists) {
		std::filesystem::remove_all(backup, ec);
		if (ec) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"ManagedScriptBuildService: LKG Backupを削除できません 内容={}", ec.message());
		}
	}
}

void Engine::ManagedBuildArtifacts::SeedLastKnownGood() {

	if (!runtime_ || !runtime_->HasLoadedGameAssembly()) {
		return;
	}
	const std::filesystem::path active = runtime_->ActiveAssemblyPath();
	if (active.empty()) {
		return;
	}
	std::error_code existsError{};
	if (!std::filesystem::exists(active, existsError) || existsError) {
		return;
	}
	const std::filesystem::path lkg = LastKnownGoodDirectory();
	// 既存LKGが同じか新しければ書き換えず、起動中の正常Assemblyが新しい場合だけ更新する
	std::error_code lkgExists{};
	if (std::filesystem::exists(lkg / kAssemblyFileName, lkgExists) && !lkgExists) {
		std::error_code activeTimeError{};
		std::error_code lkgTimeError{};
		const auto activeTime = std::filesystem::last_write_time(active, activeTimeError);
		const auto lkgTime = std::filesystem::last_write_time(lkg / kAssemblyFileName, lkgTimeError);
		if (!activeTimeError && !lkgTimeError && lkgTime >= activeTime) {
			return;
		}
	}
	UpdateLastKnownGood(active.parent_path());
}

bool Engine::ManagedBuildArtifacts::RestoreLastKnownGoodOnStartup() {

	if (!runtime_ || !runtime_->IsInitialized()) {
		return false;
	}
	const std::filesystem::path lastKnownGoodDll = LastKnownGoodDirectory() / kAssemblyFileName;
	std::error_code existsError{};
	if (!std::filesystem::exists(lastKnownGoodDll, existsError) || existsError) {
		return false;
	}

	const std::filesystem::path activeDll = runtime_->ActiveAssemblyPath();
	const bool hadLoadedActive = runtime_->HasLoadedGameAssembly();
	if (hadLoadedActive && !activeDll.empty()) {

		std::error_code activeTimeError{};
		std::error_code lkgTimeError{};
		const auto activeTime = std::filesystem::last_write_time(activeDll, activeTimeError);
		const auto lkgTime = std::filesystem::last_write_time(lastKnownGoodDll, lkgTimeError);
		// 比較できない場合はロード済みAssemblyを維持し、正常版を不必要に差し替えない
		if (activeTimeError || lkgTimeError || lkgTime <= activeTime) {
			return false;
		}
	}

	if (!runtime_->LoadGameAssemblyFromPath(lastKnownGoodDll)) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: 起動時のLastKnownGood Assembly読み込みに失敗しました path={}",
			ToUtf8Path(lastKnownGoodDll));
		// LastKnownGoodが破損していても、直前まで利用できていた現行Assemblyへ戻す
		if (hadLoadedActive && !activeDll.empty() && runtime_->LoadGameAssemblyFromPath(activeDll)) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"ManagedScriptBuildService: 起動時の現行Assemblyへ戻しました path={}", ToUtf8Path(activeDll));
		}
		return false;
	}
	if (hadLoadedActive) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptBuildService: 現行Assemblyより新しいLastKnownGoodを読み込みました path={}",
			ToUtf8Path(lastKnownGoodDll));
	} else {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptBuildService: 起動時のAssemblyを読み込めなかったためLastKnownGoodで復旧しました path={}",
			ToUtf8Path(lastKnownGoodDll));
	}
	return true;
}

std::filesystem::path Engine::ManagedBuildArtifacts::ManagedRoot() const {

	// GameRootのManagedをcsprojパスから導出する、Scripts/GameScripts.csprojからGameRoot/Managedを得る
	const std::filesystem::path projectPath = runtime_ ? runtime_->GameScriptProjectPath() : std::filesystem::path{};
	if (projectPath.empty()) {
		return {};
	}
	return projectPath.parent_path().parent_path() / "Managed";
}

std::filesystem::path Engine::ManagedBuildArtifacts::StagingRoot() const {
	return ManagedRoot() / "Staging" / BuildProfile();
}

std::filesystem::path Engine::ManagedBuildArtifacts::ShadowRoot() const {
	return ManagedRoot() / "Shadow" / BuildProfile();
}

std::filesystem::path Engine::ManagedBuildArtifacts::LastKnownGoodDirectory() const {
	return ManagedRoot() / "LastKnownGood" / BuildProfile();
}

void Engine::ManagedBuildArtifacts::PruneDirectories(const std::filesystem::path& parent) const {

	std::error_code existsError{};
	if (!std::filesystem::exists(parent, existsError) || existsError) {
		return;
	}

	// 数値ID名のサブディレクトリを集めて新しい順に保持数だけ残す
	std::vector<std::filesystem::path> directories;
	std::error_code iterateError{};
	for (const auto& entry : std::filesystem::directory_iterator(parent, iterateError)) {
		if (entry.is_directory()) {
			directories.emplace_back(entry.path());
		}
	}
	if (static_cast<int32_t>(directories.size()) <= maxRetainedDirectories_) {
		return;
	}

	// ディレクトリ名は単調増加IDなので名前長→辞書順で安定に並ぶよう数値比較する
	std::sort(directories.begin(), directories.end(),
		[](const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
			const std::wstring a = lhs.filename().wstring();
			const std::wstring b = rhs.filename().wstring();
			if (a.size() != b.size()) {
				return a.size() < b.size();
			}
			return a < b;
		});

	const size_t removeCount = directories.size() - static_cast<size_t>(maxRetainedDirectories_);
	for (size_t i = 0; i < removeCount; ++i) {

		std::error_code removeError{};
		std::filesystem::remove_all(directories[i], removeError);
		if (removeError) {
			// 掃除失敗は警告のみでリロード本体は失敗させない
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"ManagedScriptBuildService: 古いDirectoryを削除できません path={}", ToUtf8Path(directories[i]));
		}
	}
}
