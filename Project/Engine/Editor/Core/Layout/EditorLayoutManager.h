#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/Layout/EditorLayoutTypes.h>

// c++
#include <filesystem>
#include <optional>

namespace Engine {

	//============================================================================
	//	EditorLayoutManager class
	//	エディターレイアウトの保存と共有を管理するクラス
	//============================================================================
	class EditorLayoutManager {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		EditorLayoutManager() = default;
		~EditorLayoutManager() = default;

		// レイアウトカタログを読み込む
		void Init();

		// 起動時に適用するレイアウトを取得
		bool LoadStartupLayout(EditorLayoutSnapshot& outLayout);
		// ユーザーレイアウトを保存
		bool SaveUserLayout(const std::string& name, const EditorLayoutSnapshot& source,
			std::string& outLayoutID, std::string& outError);
		// 全レイアウトをエンジン共有カタログへ保存
		bool SaveAllEngineLayouts(std::string& outError);
		// エンジン共有レイアウトを取り込む
		bool ImportEngineLayouts(EditorLayoutSnapshot& outDefaultLayout, std::string& outError);
		// ユーザーレイアウトを削除
		bool DeleteLayout(const std::string& layoutID);
		// 現在セッションを保存
		void SaveSession(const EditorLayoutSnapshot& layout) const;

		//--------- accessor -----------------------------------------------------

		const std::vector<EditorLayoutMenuEntry>& GetMenuEntries() const { return menuEntries_; }
		const EditorLayoutSnapshot* FindLayout(const std::string& layoutID) const;
		const std::string& GetActiveLayoutID() const { return activeLayoutID_; }
		void SetActiveLayoutID(const std::string& layoutID) { activeLayoutID_ = layoutID; }
		bool IsEngineSourceProject() const;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct StoredLayout {

			EditorLayoutSnapshot layout;
			bool imported = false;
		};

		//--------- variables ----------------------------------------------------

		std::filesystem::path engineCatalogPath_;
		std::filesystem::path userCatalogPath_;
		std::filesystem::path sessionPath_;
		std::string engineDefaultLayoutID_ = "engine.default";
		std::string activeLayoutID_;
		std::vector<StoredLayout> engineLayouts_;
		std::vector<StoredLayout> userLayouts_;
		std::vector<EditorLayoutMenuEntry> menuEntries_;

		//--------- functions ----------------------------------------------------

		// レイアウトカタログを読み込む
		void LoadCatalog(const std::filesystem::path& path, bool imported,
			std::vector<StoredLayout>& outLayouts, std::string* outDefaultLayoutID = nullptr);
		// レイアウトカタログを保存
		void SaveCatalog(const std::filesystem::path& path, const std::vector<StoredLayout>& layouts,
			const std::string* defaultLayoutID = nullptr) const;
		// レイアウト一覧を再構築
		void RebuildMenuEntries();
		// 保存名を検証
		bool ValidateSaveName(const std::string& name, std::string& outError) const;
		// ビルトインレイアウトを作成
		EditorLayoutSnapshot MakeBuiltinDefaultLayout() const;
		// 次の並び順を取得
		int32_t GetNextUserOrder() const;
	};
} // Engine
