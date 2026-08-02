#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>

// c++
#include <deque>

namespace Engine {

	//============================================================================
	//	ShaderGraphHistory class
	//	グラフ編集単位のUndoとRedoを保持するクラス
	//============================================================================
	class ShaderGraphHistory {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphHistory() = default;
		~ShaderGraphHistory() = default;

		// 履歴を指定グラフで初期化
		void Reset(const ShaderGraphAsset& graph);
		// 編集後の状態を1操作として確定
		bool Commit(const ShaderGraphAsset& graph);
		// 直前の状態へ戻す
		bool Undo(ShaderGraphAsset& outGraph);
		// 戻した状態をやり直す
		bool Redo(ShaderGraphAsset& outGraph);

		//--------- accessor -----------------------------------------------------

		bool CanUndo() const { return !undo_.empty(); }
		bool CanRedo() const { return !redo_.empty(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		static constexpr size_t kMaximumHistory = 128;

		ShaderGraphAsset current_{};
		std::string currentState_{};
		std::deque<ShaderGraphAsset> undo_{};
		std::deque<ShaderGraphAsset> redo_{};
	};
} // Engine
