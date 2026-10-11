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
		//============================================================================
		//	public Methods
		//============================================================================

		TextureAssetResolver() = default;
		~TextureAssetResolver() = default;

		// テクスチャパスインデックスを構築
		void Build(const std::filesystem::path& modelFullPath);

		// 読込時の参照からTextureのAssetパスを解決する
		std::string ResolveAssetPath(const std::string& importedReference) const;
		// Normalを解決し失敗時は基本色の名前から補完する
		std::string ResolveNormalAssetPath(
			const std::string& importedNormalReference, const std::string& importedBaseColorReference) const;

		// Project外を含む画像の実ファイルを解決する
		std::filesystem::path ResolveFilePath(const std::string& importedReference) const;
		// 基本色からのNormal補完も実ファイルとして解決する
		std::filesystem::path ResolveNormalFilePath(
			const std::string& importedNormalReference, const std::string& importedBaseColorReference) const;

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// 実ファイルとAsset参照を対応させる候補
		struct TextureCandidate {

			// テクスチャのフルパス
			std::filesystem::path fullPath;
			std::string assetPath; // Project外では未登録
			std::string stemLower; // 名前による補完のキー
			std::string extLower;  // 形式の優先順位

			// テクスチャが優先フォルダに存在するか
			bool inPreferredFolder = false;
		};

		//--------- variables ----------------------------------------------------

		std::filesystem::path texturesRoot_;											  // Engine側の画像ルート
		std::filesystem::path modelDirectory_;											  // 直接参照の基準
		std::filesystem::path preferredFolder_;											  // モデル専用の画像フォルダー
		std::unordered_map<std::string, std::vector<TextureCandidate>> candidatesByStem_; // 名前ごとの候補
		bool uriReferences_ = false; // 外部参照がURIの形式か

		//--------- functions ----------------------------------------------------

		// ファイル名から拡張子を除いた部分を小文字に変換して返す
		static std::string NormalizeStem(std::string_view name);
		// ファイルの拡張子がテクスチャ形式かどうかを判定
		static bool IsTextureExtension(const std::filesystem::path& path);
		// ファイルのフルパスから、テクスチャアセットの参照パスを生成する
		static std::string ToAssetPath(const std::filesystem::path& fullPath);

		// テクスチャ候補を再帰的にインデックスに追加
		void IndexDirectoryRecursive(const std::filesystem::path& directory, bool inPreferredFolder);
		// テクスチャ候補のリストから、最適な候補を選択する
		const TextureCandidate* ChooseBestCandidate(const std::vector<TextureCandidate>& candidates, bool filePaths) const;
		// 同じstemの候補を解決先の範囲で選ぶ
		const TextureCandidate* ResolveIndexedPathByStem(const std::string& stemLower, bool filePaths) const;
		// 直接参照と索引の順に画像を解決する
		std::filesystem::path ResolvePath(const std::string& importedReference, bool filePaths) const;
		// 明示参照と名前補完の順にNormalを解決する
		std::filesystem::path ResolveNormalPath(const std::string& normal, const std::string& baseColor, bool filePaths) const;
	};
} // Engine
