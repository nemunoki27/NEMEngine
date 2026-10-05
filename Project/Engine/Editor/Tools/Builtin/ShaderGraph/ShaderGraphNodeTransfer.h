#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphEditSession.h"

// c++
#include <span>

namespace Engine {

	//============================================================================
	//	ShaderGraphNodeTransfer class
	//	Nodeの複製とクリップボード転送を編集Graphへ適用する
	//============================================================================
	class ShaderGraphNodeTransfer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphNodeTransfer(ShaderGraphEditSession& session);

		// 出力Nodeを除いて複製する
		void DuplicateNode(UUID nodeID);
		// 選択Nodeと内部接続をコピーする
		void CopySelection(std::span<const UUID> selected);
		// 新しいIDで貼り付けて選択する
		bool PasteSelection();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// コピー元と配置先の編集session
		ShaderGraphEditSession& session_;
	};
} // Engine
