#pragma once

//============================================================================
//	include
//============================================================================
#include <filesystem>
#include <cstdint>

namespace Engine { class ManagedScriptRuntime; }

namespace Engine {

	//============================================================================
	//	ManagedBuildArtifacts class
	//	成果物の配置と正常版を管理する
	//============================================================================
	class ManagedBuildArtifacts {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ManagedBuildArtifacts(ManagedScriptRuntime*& runtime);

		// 必須成果物を検証する
		bool ValidateArtifacts(const std::filesystem::path& directory) const;
		// 成果物を配置する
		bool CopyArtifacts(const std::filesystem::path& from, const std::filesystem::path& to) const;
		// 正常版を更新する
		void UpdateLastKnownGood(const std::filesystem::path& shadowDirectory);
		// ロード済みAssemblyを正常版へ保存する
		void SeedLastKnownGood();
		// 起動時に正常版を復旧する
		bool RestoreLastKnownGoodOnStartup();
		// 作業ルートを取得する
		std::filesystem::path ManagedRoot() const;
		// 構築先を取得する
		std::filesystem::path StagingRoot() const;
		// 読込先を取得する
		std::filesystem::path ShadowRoot() const;
		// 正常版の保存先を取得する
		std::filesystem::path LastKnownGoodDirectory() const;
		// 保持数を超えた作業先を整理する
		void PruneDirectories(const std::filesystem::path& parent) const;

		//--------- accessor -----------------------------------------------------

		bool HasUpdateFailed() const { return lastKnownGoodUpdateFailed_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ManagedScriptRuntime*& runtime_;
		bool lastKnownGoodUpdateFailed_ = false;
		int32_t maxRetainedDirectories_ = 3;
	};
}
