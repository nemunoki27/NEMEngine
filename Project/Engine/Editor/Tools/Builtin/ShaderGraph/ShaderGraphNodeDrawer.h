#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphNodeValueEditor.h"
#include "ShaderGraphNodePreviews.h"
#include "ShaderGraphAppearance.h"

// c++
#include <unordered_map>

namespace Engine {

	// 入出力Pinの接続先
	struct ShaderGraphPinAddress {

		UUID node{};
		uint32_t slot = 0;
		bool input = false;
	};

	//============================================================================
	//	ShaderGraphNodeDrawer class
	//	NodeとPinの表示と値編集を保持する
	//============================================================================
	class ShaderGraphNodeDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphNodeDrawer(
			ShaderGraphEditSession& session, const ShaderGraphAppearanceSetting& settings, ShaderGraphNodePreviews& previews);

		// Nodeの本体とPinを表示する
		void DrawNode(ShaderGraphNode& node);
		// 予約した値編集popupを表示する
		void DrawPopup();
		// 前frameのPin対応を解除する
		void ClearPins();
		// Graph切替時に編集状態を解除する
		void Reset();

		//--------- accessor -----------------------------------------------------

		const auto& GetPinAddresses() const { return pinAddresses_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 所有Toolより先に参照先を破棄しない
		ShaderGraphEditSession& session_;
		const ShaderGraphAppearanceSetting& settings_;
		ShaderGraphNodePreviews& previews_;
		std::unordered_map<uintptr_t, ShaderGraphPinAddress> pinAddresses_;
		ShaderGraphNodeValueEditor nodeValueEditor_;

		//--------- functions ----------------------------------------------------

		// Nodeの入出力Pinを表示する
		void DrawNodePins(const ShaderGraphNode& node, float nodeWidth);
		// Node内の区切り線を描く
		void DrawNodeSeparator(float nodeWidth) const;
		// Nodeの内容から表示幅を求める
		float CalculateNodeWidth(const ShaderGraphNode& node) const;
	};
} // Engine
