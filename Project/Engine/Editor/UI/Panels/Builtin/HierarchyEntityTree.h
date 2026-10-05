#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Editor/UI/Common/TextSearchFilter.h>

namespace Engine {

	class TextureUploadService;

	//============================================================================
	//	HierarchyEntityTree class
	//	Entity階層の検索と選択表示
	//============================================================================
	class HierarchyEntityTree {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit HierarchyEntityTree(TextureUploadService& textureUploadService);
		~HierarchyEntityTree() = default;

		// 検索欄と表示アイコンを準備する
		void DrawSearch();
		// 行の表示順を初期化する
		void BeginFrame();
		// Entityと子階層を表示する
		void DrawEntityNode(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool forceVisible);
		// Entityか子孫が検索条件に一致するか
		bool ShouldDrawEntityNode(ECSWorld& world, const Entity& entity) const;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// Applicationが所有するTextureの要求窓口
		TextureUploadService& textureUploadService_;
		// 表示アイコンの要求済み状態
		bool activeIconRequested_ = false;
		// 交互に背景を塗る行番号
		uint32_t visibleEntityRowIndex_ = 0;
		// Entity名の検索条件
		TextSearchFilter searchFilter_;

		//--------- functions ----------------------------------------------------

		// アクティブ表示のアイコンを読み込む
		void RequestActiveIconTextures();
		// アクティブ状態の切替ボタンを表示する
		void DrawActiveToggleIcon(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool activeSelf,
			bool& leftClicked, bool& rightClicked);
		// Entity名が検索条件に一致するか
		bool EntityMatchesSearch(ECSWorld& world, const Entity& entity) const;
	};
} // Engine
