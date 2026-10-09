#pragma once

//============================================================================
//	include
//============================================================================
#include <chrono>
#include <filesystem>
#include <future>
#include <string>

namespace Engine {

	struct ProjectGitIgnoreState {

		std::filesystem::path target;
		std::filesystem::path repository;
		std::string relativePath;
		std::string message;
		bool valid = false;
		bool directory = false;
		bool excluded = false;
		bool targetExcluded = false;
		bool metaExcluded = false;
		bool parentExcluded = false;
		bool tracked = false;
	};

	//============================================================================
	//	ProjectGitIgnoreService class
	//	Gitの状態取得と除外設定の保存をworkerへ渡す
	//============================================================================
	class ProjectGitIgnoreService {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 完了した処理を取り込み、保存結果を返す
		std::string Poll();
		// 指定項目の状態を要求する
		const ProjectGitIgnoreState* Request(const std::filesystem::path& path, bool directory);
		// Gitの索引を変更せず除外設定だけを保存する
		void SetIncluded(bool included);
		bool IsBusy() const;
		bool HasError() const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structures ---------------------------------------------------

		struct Result {

			ProjectGitIgnoreState state;
			std::string message;
		};

		//--------- variables ----------------------------------------------------

		std::future<Result> pending_;
		ProjectGitIgnoreState state_;
		std::chrono::steady_clock::time_point refreshed_{};

		//--------- functions ----------------------------------------------------

		static ProjectGitIgnoreState Query(const std::filesystem::path& path, bool directory);
		static Result Apply(const std::filesystem::path& path, bool directory, bool included);
	};
}
