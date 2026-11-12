#pragma once

//============================================================================
//	include
//============================================================================
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace Engine::EditorShell {

	//============================================================================
	//	PathSelectionDialog class
	//	エディターを停止せずにファイルとフォルダーの選択を管理する
	//============================================================================
	class PathSelectionDialog {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		PathSelectionDialog();
		~PathSelectionDialog();

		PathSelectionDialog(const PathSelectionDialog&) = delete;
		PathSelectionDialog& operator=(const PathSelectionDialog&) = delete;

		// フォルダ選択ダイアログを開く
		bool Open(const std::filesystem::path& initialDirectory = {});
		// 指定した種類のファイルを選択する
		bool OpenFile(const std::filesystem::path& initialDirectory = {}, const std::wstring& filter = L"*.*");
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

		// 選択方法を固定してダイアログを起動する
		bool Start(const std::filesystem::path& initialDirectory, bool folder, const std::wstring& filter);
	};

	using DirectorySelectionDialog = PathSelectionDialog;

	// OS既定のアプリでファイルを開く
	bool OpenWithSystemDefault(const std::filesystem::path& file);
	// 指定したディレクトリをエクスプローラーで開く
	bool OpenDirectory(const std::filesystem::path& directory);
	// URLを既定のブラウザーで開く
	bool OpenURL(const std::string& url);

} // Engine::EditorShell
