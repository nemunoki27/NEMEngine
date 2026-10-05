#pragma once

//============================================================================
//	include
//============================================================================
#include "ImGuiHelpers.h"
#include <Engine/Core/Foundation/Utility/Enum/Axis.h>
#include <imgui_internal.h>

namespace Engine::CurveEditorUtility {

	// 上部ボタンサイズ
	inline constexpr ImVec2 kCurveToolbarItemSize = ImVec2(80.0f, 20.0f);
	// 右キーインスペクタの幅
	inline constexpr float kCurveInspectorWidth = 160.0f;
	// 右キーインスペクタのアイテムサイズ
	inline constexpr float kCurveInspectorItemWidth = 80.0f;
	// キー点の表示半径
	inline constexpr float kCurveKeyRadius = 4.0f;
	// カーブエディター内で共通の文字スケール
	inline constexpr float kCurveEditorFontScale = 0.72f;
	// 主グリッドとして扱う間隔
	inline constexpr int32_t kCurveGridMajorInterval = 8;
	// ルーラー領域
	inline constexpr float kCurveTopRulerHeight = 22.0f;
	inline constexpr float kCurveLeftRulerWidth = 52.0f;
	// 軸線色
	inline constexpr ImU32 kCurveAxisColor = IM_COL32(255, 210, 80, 255);

	// 表示開始時刻を負にしない
	float ClampVisibleTimeMin(float v);

	// 時間軸の表示範囲を更新する
	void UpdateHorizontalViewRange(const ImRect& graphRect, Engine::CurveEditorState& state);

	// 固定時間範囲を表示へ反映する
	void ApplyFixedTimeRange(const ImRect& graphRect, const Engine::CurveEditSetting& setting, Engine::CurveEditorState& state);

	// 値範囲に表示倍率を合わせる
	void UpdateVerticalZoomFromRange(const ImRect& graphRect, Engine::CurveEditorState& state);

	// キー値を表示範囲へ収める
	float ClampKeyValueToVisibleRange(const Engine::CurveEditorState& state, float value);

	// 現在の範囲に目盛り間隔を合わせる
	void RefreshGridStepsOnly(const ImRect& graphRect, Engine::CurveEditorState& state);

	// マウス位置を基準に時間軸を拡縮する
	void ZoomTimeAroundMouse(
		const ImRect& graphRect, Engine::CurveEditorState& state, const ImVec2& mousePos, float wheelDelta);

	// 有効な間隔へキー時刻を丸める
	float SnapTime(float time, bool enableSnap, float interval);

	// キー時刻を許可範囲へ収める
	float ClampKeyTime(const Engine::CurveEditorState& state, float time);

	// 目盛り間隔に表示桁数を合わせる
	std::string FormatGridValue(float value, float step);

	// カーブ座標を画面座標へ変換する
	ImVec2 WorldToScreen(const ImRect& rect, const Engine::CurveEditorState& state, float time, float value);

	// 画面座標をカーブ座標へ変換する
	ImVec2 ScreenToWorld(const ImRect& rect, const Engine::CurveEditorState& state, const ImVec2& pos);

	// 矩形内のマウス位置を判定する
	bool RectContains(const ImRect& rect, const ImVec2& pos);

	// 色のチャンネル構成を判定する
	bool IsColorChannelSet(std::span<Engine::CurveChannel> channels);

	// 軸と角度の構成を判定する
	bool IsQuaternionCurveSet(std::span<Engine::CurveChannel> channels);

	// 軸の表示色を返す
	ImU32 GetAxisColor(float value);

	// 任意の軸キー列を借用する
	std::span<Engine::CurveQuaternionAxisKey> ToAxisKeySpan(std::vector<Engine::CurveQuaternionAxisKey>* axisKeys);

	// 代表軸のキー値を取得する
	float GetPrimaryAxisValue(const Engine::CurveQuaternionAxisKey& axisKey);

	// 軸の表示色を返す
	ImU32 GetAxisColor(const Engine::CurveQuaternionAxisKey& axisKey);

	// 軸キーか既定軸を取得する
	Engine::CurveQuaternionAxisKey GetQuaternionAxisKey(
		std::span<Engine::CurveChannel> channels, std::span<Engine::CurveQuaternionAxisKey> axisKeys, uint32_t keyIndex);

	// 軸情報を表示チャンネルへ同期する
	void SyncQuaternionAxisChannel(
		std::span<Engine::CurveChannel> channels, std::span<Engine::CurveQuaternionAxisKey> axisKeys, uint32_t keyIndex);

	// 矩形の始点と終点を揃える
	void NormalizeRect(ImVec2& minPos, ImVec2& maxPos);

	// キー全体が収まる表示範囲にする
	void FitView(const ImRect& graphRect, std::span<Engine::CurveChannel> channels, Engine::CurveEditorState& state);

	// RGBカーブの構成を判定する
	bool IsColorCurveSet(std::span<Engine::CurveChannel> channels);

	// Alphaチャンネルの有無を確認する
	bool HasAlphaChannel(std::span<Engine::CurveChannel> channels);

	// 軸と角度のキー選択を検証する
	bool IsQuaternionSelection(std::span<Engine::CurveChannel> channels, const Engine::CurveKeySelection& selection);

	// 軸キーの選択を検証する
	bool IsQuaternionAxisSelection(std::span<Engine::CurveChannel> channels, const Engine::CurveKeySelection& selection);

	// 角度キーの選択を検証する
	bool IsQuaternionAngleSelection(std::span<Engine::CurveChannel> channels, const Engine::CurveKeySelection& selection);

	// RGB代表キーの選択を検証する
	bool IsRGBSelection(std::span<Engine::CurveChannel> channels, const Engine::CurveKeySelection& selection);

	// Alphaキーの選択を検証する
	bool IsAlphaSelection(std::span<Engine::CurveChannel> channels, const Engine::CurveKeySelection& selection);

	// 指定時刻の色を評価する
	Engine::Color4 EvaluateCurveColorAtTime(std::span<Engine::CurveChannel> channels, float time);

	// RGBキーを同じ時刻へ追加する
	uint32_t AddColorRGBKey(std::span<Engine::CurveChannel> channels, float time);

	// RGBキーをまとめて削除する
	bool RemoveColorRGBKey(std::span<Engine::CurveChannel> channels, uint32_t keyIndex);

	// RGBキーを時刻順に揃える
	void SortColorRGBKeys(std::span<Engine::CurveChannel> channels);

	// 評価した軸を新しいキーへ引き継ぐ
	uint32_t AddQuaternionAxisKey(
		std::span<Engine::CurveChannel> channels, std::vector<Engine::CurveQuaternionAxisKey>* axisKeys, float time);

	// 評価した角度を新しいキーへ引き継ぐ
	uint32_t AddQuaternionAngleKey(std::span<Engine::CurveChannel> channels, float time);

	// 選択した軸か角度のキーを削除する
	bool RemoveQuaternionKey(std::span<Engine::CurveChannel> channels, std::vector<Engine::CurveQuaternionAxisKey>* axisKeys,
		const Engine::CurveKeySelection& selection);

	// 軸と補助情報の順番を揃える
	void SortQuaternionKeys(std::span<Engine::CurveChannel> channels, std::vector<Engine::CurveQuaternionAxisKey>* axisKeys);

	// キーの選択状態を取得する
	bool IsKeySelected(const Engine::CurveEditorState& state, uint32_t channelIndex, uint32_t keyIndex);

	// マウスに近いキーを探す
	bool HitTestKey(const ImRect& rect, std::span<Engine::CurveChannel> channels, const Engine::CurveEditorState& state,
		const ImVec2& mouse, Engine::CurveKeySelection& outSelection);

	// 表示範囲の目盛りを描く
	void DrawGrid(const ImRect& rect, const Engine::CurveEditorState& state);

	// 時間軸の目盛りを描く
	void DrawTimeRuler(const ImRect& rect, const Engine::CurveEditorState& state);

	// 値軸の目盛りを描く
	void DrawValueRuler(const ImRect& rect, const Engine::CurveEditorState& state);

	// 評価したカーブを線で描く
	void DrawCurveSamples(const ImRect& rect, const Engine::CurveChannel& channel, const Engine::CurveEditorState& state);

	// RGBカーブを色付きで描く
	void DrawColorRGBCurveSamples(
		const ImRect& rect, std::span<Engine::CurveChannel> channels, const Engine::CurveEditorState& state);

	// 軸と色と通常キーを描く
	void DrawKeys(const ImRect& rect, std::span<Engine::CurveChannel> channels, const Engine::CurveEditorState& state,
		std::span<Engine::CurveQuaternionAxisKey> quaternionAxisKeys);

	// 現在時刻の線を描く
	void DrawCurrentTimeLine(const ImRect& rect, const Engine::CurveEditorState& state);

	// 選択キーを後ろから削除する
	void DeleteSelectedKeys(std::span<Engine::CurveChannel> channels, Engine::CurveEditorState& state,
		std::vector<Engine::CurveQuaternionAxisKey>* quaternionAxisKeys);

	// 選択チャンネルのキーを削除する
	void DeleteSelectedChannelKeys(std::span<Engine::CurveChannel> channels, Engine::CurveEditorState& state,
		std::vector<Engine::CurveQuaternionAxisKey>* quaternionAxisKeys);

	// 時刻と表示操作を表示する
	void DrawToolbar(
		std::span<Engine::CurveChannel> channels, Engine::CurveEditorState& state, Engine::CurveEditResult& result);

	// 選択キーの時刻と値を編集する
	void DrawInspector(std::span<Engine::CurveChannel> channels, Engine::CurveEditorState& state,
		Engine::CurveEditResult& result, float height, std::vector<Engine::CurveQuaternionAxisKey>* quaternionAxisKeys);

	// マウスとキー操作をカーブへ反映する
	void HandleGraphInput(const ImRect& graphRect, std::span<Engine::CurveChannel> channels, Engine::CurveEditorState& state,
		Engine::CurveEditResult& result, std::vector<Engine::CurveQuaternionAxisKey>* quaternionAxisKeys);

	// 表示する値の上下限を編集する
	void DrawGraphRangeEditors(const ImRect& topCornerRect, const ImRect& bottomCornerRect, Engine::CurveEditorState& state,
		bool& outValueChanged, bool& outEditFinished, bool& outUIBlocking);
} // Engine::CurveEditorUtility
