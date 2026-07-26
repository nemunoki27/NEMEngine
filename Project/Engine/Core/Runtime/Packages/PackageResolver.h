#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	PackageResolver structures
	//============================================================================
	struct ResolvedPackage {

		std::string name;
		std::string version;
		std::string source;
		std::filesystem::path root;
		uint64_t contentHash = 0;
	};

	struct PackageResolveIssue {

		std::string packageName;
		std::string detail;
	};

	struct PackageResolveResult {

		std::vector<ResolvedPackage> packages;
		std::vector<PackageResolveIssue> issues;

		bool Succeeded() const { return issues.empty(); }
	};

	//============================================================================
	//	PackageResolver class
	//	プロジェクトのパッケージ依存を再現可能なマウント一覧へ解決するクラス
	//============================================================================
	class PackageResolver {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		PackageResolver() = delete;
		~PackageResolver() = delete;

		// manifestを解決してlock fileを更新
		static PackageResolveResult Resolve(const std::filesystem::path& projectRoot,
			const std::filesystem::path& packagesRoot,
			const std::filesystem::path& libraryRoot);
	};
} // Engine
