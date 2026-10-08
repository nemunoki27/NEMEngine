#pragma once

//============================================================================
//	include
//============================================================================
#include "ImGuiHelpers.h"
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>

// c++
#include <array>
#include <string>
#include <initializer_list>

namespace Engine {

	// front
	class AssetDatabase;
	class GraphicsCore;

	//============================================================================
	//	MyGUI internal helpers
	//	Widget間で共有する内部処理
	//============================================================================

	// 軸ラベルの表示用途
	enum class AxisLabelPalette {

		Editable,
		Copyable,
	};

	// 軸ラベルの名前と表示色
	struct AxisDisplayInfo {

		const char* name;
		ImVec4 color;
	};

	// 表示用途に応じた軸ラベルを取得する
	AxisDisplayInfo GetAxisDisplayInfo(char axis, AxisLabelPalette palette = AxisLabelPalette::Editable);

	// 精度を指定してfloatを文字列へ変換する
	std::string FormatFloat(float value, uint32_t precision);

	// プロパティ行に読み取り専用の数値フィールド群を描画する
	void DrawTextFields(const char* label, const std::array<char, 4>& axes,
		const float* values, uint32_t count, uint32_t precision);
	// プロパティ行に軸ラベル付きのドラッグ編集フィールド群を描画する
	ValueEditResult DrawDragFields(const char* label, const std::array<char, 4>& axes,
		float* values, uint32_t count, const FloatEditSetting& setting);

	// アセット参照UIで使う共有ヘルパー
	std::string MakeAssetDisplayNameFromPath(const std::string& assetPath);
	ImTextureID ResolveTextureAssetPreview(GraphicsCore* graphicsCore, const AssetDatabase* assetDatabase, AssetID assetID);
	AssetType GuessDroppedAssetType(const EditorAssetDragDropPayload& payload);
	bool IsAcceptedAssetType(AssetType type, const std::initializer_list<AssetType>& acceptedTypes);
	bool TryReadAssetPayload(const ImGuiPayload* payload, EditorAssetDragDropPayload& outPayload);
	bool TryReadEntityPayload(const ImGuiPayload* payload, UUID& outUUID);

} // Engine
