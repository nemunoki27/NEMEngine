#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphEditSession.h"
#include "ShaderGraphAppearance.h"

// c++
#include <span>

namespace Engine {

	//============================================================================
	//	ShaderGraphGroupEditor class
	//	Groupの表示と所属編集と名前入力を保持する
	//============================================================================
	class ShaderGraphGroupEditor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphGroupEditor(ShaderGraphEditSession& session, const ShaderGraphAppearanceSetting& settings);

		// Groupの範囲と名前を表示する
		void DrawGroup(ShaderGraphGroup& group);
		// Groupの所属を解除して削除する
		void RemoveGroup(UUID groupID);
		// Group内のNodeと内部接続を複製する
		void DuplicateGroup(UUID groupID);
		// 選択NodeをGroupへまとめる
		void GroupSelectedNodes(std::span<const UUID> selectedNodes);
		// Graph切替時に名前入力を解除する
		void Reset();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ShaderGraphEditSession& session_;
		const ShaderGraphAppearanceSetting& settings_;
		UUID editingGroup_{};
		bool requestGroupNameFocus_ = false;
	};
} // Engine
