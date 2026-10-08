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
		// 選択結果を取得する、キャンセル時は空を返す
		bool Poll(std::optional<std::filesystem::path>& outSelected);

		//--------- accessor -----------------------------------------------------

		// 選択結果の取得待ちか判定する
		bool IsOpen() const;

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct State;

		//--------- variables ----------------------------------------------------

		// 終了を待ってから破棄するダイアログの状態
		std::unique_ptr<State> state_;
	};

	// OS既定のアプリでファイルを開く
	bool OpenWithSystemDefault(const std::filesystem::path& file);
	// 指定したディレクトリをエクスプローラーで開く
	bool OpenDirectory(const std::filesystem::path& directory);

} // Engine::EditorShell
