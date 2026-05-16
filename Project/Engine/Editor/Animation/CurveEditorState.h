#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <cstdint>
#include <vector>
// imgui
#include <imgui.h>

namespace Engine {

	//============================================================================
	//	CurveEditor structures
	//============================================================================

	// 選択中キーをチャンネル番号とキー番号で表す
	struct CurveKeySelection {

		// チャンネルとキーのインデックス
		uint32_t channelIndex = 0;
		uint32_t keyIndex = 0;

		bool operator==(const CurveKeySelection& other) const {
			return channelIndex == other.channelIndex && keyIndex == other.keyIndex;
		}
	};
	// カーブエディター上で現在行っているドラッグ操作
	enum class CurveEditorDragMode :
		uint8_t {

		None,    // 操作なし
		Key,     // キーの移動
		Marquee, // 矩形選択
		Pan,     // 表示範囲の移動
	};
	// カーブエディター描画時の設定
	struct CurveEditSetting {

		// エディター全体の表示サイズ
		ImVec2 size = ImVec2(0.0f, 320.0f);
		// 有効範囲に合わせて表示範囲を自動で調整するか
		bool autoFit = false;
		// 上部ツールバーを表示するか
		bool showToolbar = true;
		// 左右の補助パネルを表示するか
		bool showSidePanels = true;
		// 新規キーやキー移動時に時間をスナップするか
		bool snap = true;
		// スナップする時間間隔
		float snapInterval = 0.03f;
	};
	// カーブ編集操作の結果
	struct CurveEditResult {

		// カーブ値が変更されたか
		bool valueChanged = false;
		// 選択状態が変更されたか
		bool selectionChanged = false;
		// 編集中のImGui itemがあるか
		bool anyItemActive = false;
		// ドラッグや数値編集が確定したか
		bool editFinished = false;
	};

	struct CurveEditorState {

		// 表示している時間範囲の最小値
		float visibleTimeMin = 0.0f;
		// 表示している時間範囲の最大値
		float visibleTimeMax = 1.0f;
		// 表示している値範囲の最小値
		float visibleValueMin = -1.0f;
		// 表示している値範囲の最大値
		float visibleValueMax = 1.0f;
		// 現在時刻カーソル
		float currentTime = 0.0f;
		// editor state側で保持するスナップ有効状態
		bool snapEnabled = true;
		// editor state側で保持するスナップ間隔
		float snapInterval = 0.01f;
		// グリッド線の間隔
		float gridTimeStep = 0.25f;
		float gridValueStep = 0.25f;

		// Time方向のズーム倍率
		float pixelsPerSecond = 120.0f;
		// Value方向のズーム倍率
		float pixelsPerValue = 80.0f;

		// 右クリックした位置のワールド座標
		ImVec2 contextMenuWorld = ImVec2(0.0f, 0.0f);
		// 右クリック時にヒットしていたキー
		CurveKeySelection contextMenuKey{};
		bool contextMenuOnKey = false;

		// 64chまでを簡易的にON/OFFする表示マスク
		uint64_t activeChannelMask = ~0ull;
		// チャンネルごとの表示状態
		std::vector<bool> channelVisible;
		// 選択中キーのリスト
		std::vector<CurveKeySelection> selectedKeys;
		// hover中のキー
		CurveKeySelection hoveredKey{};
		// キーにhoverしているか
		bool hasHoveredKey = false;

		// 現在のドラッグ操作
		CurveEditorDragMode dragMode = CurveEditorDragMode::None;
		// ドラッグ開始時のマウス座標
		ImVec2 dragStartMouse = ImVec2(0.0f, 0.0f);
		// 前フレームのマウス座標
		ImVec2 dragLastMouse = ImVec2(0.0f, 0.0f);
		// 矩形選択の開始/終了座標
		ImVec2 marqueeMin = ImVec2(0.0f, 0.0f);
		ImVec2 marqueeMax = ImVec2(0.0f, 0.0f);
		// 矩形選択中か
		bool marqueeActive = false;
		// 次の描画でFit Viewを行う要求
		bool frameSelectionRequest = false;

		// 指定チャンネルが表示対象か
		bool IsChannelVisible(uint32_t channelIndex) const;
		// 指定チャンネルの表示状態を設定する
		void SetChannelVisible(uint32_t channelIndex, bool visible);
		// チャンネル表示配列を必要数まで拡張する
		void EnsureChannelCount(uint32_t channelCount);
		// 選択中キーをすべて解除する
		void ClearSelection();
		// 指定キーが選択されているか
		bool IsSelected(uint32_t channelIndex, uint32_t keyIndex) const;
		// 指定キーだけを選択する
		void SelectSingle(uint32_t channelIndex, uint32_t keyIndex);
		// 指定キーの選択状態を反転する
		void ToggleSelection(uint32_t channelIndex, uint32_t keyIndex);
		// 削除やソート後に無効になった選択を取り除く
		void RemoveInvalidSelections(uint32_t channelCount, const uint32_t* keyCounts);
	};
} // Engine
