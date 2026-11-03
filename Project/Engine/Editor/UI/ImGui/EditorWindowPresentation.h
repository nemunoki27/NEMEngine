#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <string>
#include <string_view>
// imgui
#include <imgui.h>

namespace Engine::EditorWindowPresentation {

	// 表示名とアイコンを内部IDから分離する
	std::string MakeWindowName(std::string_view displayName, std::string_view identity, std::string_view iconKey);
	// パネルとツールに割り当てたアイコンを取得する
	const char* GetIcon(std::string_view key);
	// OSのタイトルへ渡す名前からアイコンを取り除く
	const char* GetPlainTitle(const char* title);
	// フォントへ組み込まれたアイコンを使用できるか確認する
	bool HasIconFont();
	// 標準フォントへエディターのアイコンを合成する
	void ConfigureFont(ImGuiIO& io);
	// 共通のタイトル表示でウィンドウを開始する
	bool Begin(const char* name, bool* open = nullptr, ImGuiWindowFlags flags = 0);
	// 保存済みレイアウトでもタブを表示する
	void PrepareDockNodes();

	//============================================================================
	//	HeaderScope class
	//	タイトルとドッキング領域だけに共通の外観を適用する
	//============================================================================
	class HeaderScope {
	public:
		HeaderScope();
		~HeaderScope();
		HeaderScope(const HeaderScope&) = delete;
		HeaderScope& operator=(const HeaderScope&) = delete;
	};
}
