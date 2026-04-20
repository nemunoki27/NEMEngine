#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	TextureAssetResolver class
	//	テクスチャ参照パスを解決するクラス
	//============================================================================
	class TextureAssetResolver {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		TextureAssetResolver() = default;
		~TextureAssetResolver() = default;

		// テクスチャパスインデックスを構築
		void Build(const std::filesystem::path& modelFullPath);

		// インポートされたテクスチャ参照から、テクスチャアセットのパスを解決して返す
		std::string ResolveAssetPath(const std::string& importedReference) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct TextureCandidate {

			// テクスチャのフルパス
			std::filesystem::path fullPath;
			std::string assetPath;
			std::string stemLower;
			std::string extLower;

			// テクスチャが優先フォルダに存在するか
			bool inPreferredFolder = false;
		};

		//--------- variables ----------------------------------------------------

		std::filesystem::path texturesRoot_ = "Engine/Assets/Textures";
		std::filesystem::path preferredFolder_;
		std::unordered_map<std::string, std::vector<TextureCandidate>> candidatesByStem_;

		//--------- functions ----------------------------------------------------

		// ファイル名から拡張子を除いた部分を小文字に変換して返す
		static std::string NormalizeStem(const std::string_view& name);
		// ファイルの拡張子がテクスチャ形式かどうかを判定
		static bool IsTextureExtension(const std::filesystem::path& path);
		// ファイルのフルパスから、テクスチャアセットの参照パスを生成する
		static std::string ToAssetPath(const std::filesystem::path& fullPath);

		// テクスチャ候補を再帰的にインデックスに追加
		void IndexDirectoryRecursive(const std::filesystem::path& directory, bool inPreferredFolder);
		// テクスチャ候補のリストから、最適な候補を選択する
		const TextureCandidate* ChooseBestCandidate(const std::vector<TextureCandidate>& candidates) const;
	};
} // Engine