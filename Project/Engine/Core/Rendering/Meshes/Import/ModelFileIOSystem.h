#pragma once

//============================================================================
//	include
//============================================================================
#include "ModelFileDependencyCollector.h"
// c++
#include <filesystem>
#include <string>
#include <string_view>

// assimp
#include <assimp/DefaultIOSystem.h>

namespace Engine {

	//============================================================================
	//	ModelFileIOSystem class
	//	モデルの読込と依存収集で外部ファイルの解決を共有する
	//============================================================================
	class ModelFileIOSystem final : public Assimp::IOSystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit ModelFileIOSystem(const std::filesystem::path& model,
			ModelFileDependencyCollector::ModelDependencies* dependencies = nullptr);
		~ModelFileIOSystem() override = default;
		ModelFileIOSystem(const ModelFileIOSystem&) = delete;
		ModelFileIOSystem& operator=(const ModelFileIOSystem&) = delete;

		// Assimpへ渡す本体パスの区切りを揃える
		const std::string& GetModelPath() const { return modelPath_; }

		// モデルと外部参照の実ファイルを確認する
		bool Exists(const char* path) const override;
		// 外部参照を開き、読込成功したファイルを記録する
		Assimp::IOStream* Open(const char* path, const char* mode) override;
		// 実ファイルへ解決して同じ参照か確認する
		bool ComparePaths(const char* first, const char* second) const override;
		// 標準の区切りとStream解放を使う
		char getOsSeparator() const override;
		void Close(Assimp::IOStream* stream) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Assimp::DefaultIOSystem fileSystem_; // 実ファイルの読込とStreamを管理
		std::string modelPath_;      // URIへ変換しないモデル本体のパス
		std::string directory_;      // URIへ変換しない参照元のフォルダー
		bool uriReferences_ = false; // 外部参照がURIの形式か
		ModelFileDependencyCollector::ModelDependencies* dependencies_ = nullptr; // 呼出し元が所有する記録先

		//--------- functions ----------------------------------------------------

		// パスの区切りだけを比較用に揃える
		static std::string NormalizeSeparators(std::string_view path);
		// 本体の文字列を保持し外部参照だけを復号する
		std::filesystem::path ResolvePath(std::string_view path) const;
	};
}
