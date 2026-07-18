#pragma once

//============================================================================
//	include
//============================================================================
#include <filesystem>
#include <memory>
#include <optional>

namespace Engine::EditorShell {

	//============================================================================
	//	DirectorySelectionDialog class
	//	エディターを停止せずにフォルダ選択ダイアログを管理する
	//============================================================================
	class DirectorySelectionDialog {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		DirectorySelectionDialog();
		~DirectorySelectionDialog();

		DirectorySelectionDialog(const DirectorySelectionDialog&) = delete;
		DirectorySelectionDialog& operator=(const DirectorySelectionDialog&) = delete;

		// フォルダ選択ダイアログを開く
		bool Open(const std::filesystem::path& initialDirectory = {});
		// 完了結果を取得、キャンセル時は空のoptionalを返す
		bool Poll(std::optional<std::filesystem::path>& outSelected);

		//--------- accessor -----------------------------------------------------

		bool IsOpen() const;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct State;

		//--------- variables ----------------------------------------------------

		std::unique_ptr<State> state_;
	};

	// OS既定の関連付けでファイルを開く、txtなど専用エディタを持たないアセット向け
	bool OpenWithSystemDefault(const std::filesystem::path& file);
	// 指定したディレクトリをエクスプローラーで開く
	bool OpenDirectory(const std::filesystem::path& directory);

} // Engine::EditorShell
