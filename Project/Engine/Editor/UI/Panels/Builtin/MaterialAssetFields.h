#pragma once

namespace Engine {

	struct EditorPanelContext;
	struct MaterialAsset;
	struct ValueEditResult;

	// MaterialのPassと公開値を編集する
	namespace MaterialAssetFields {

		// 編集値を表示して保存要求を返す
		ValueEditResult Draw(const EditorPanelContext& context, MaterialAsset& draft);
		ValueEditResult DrawPaintSettings(const EditorPanelContext& context, MaterialAsset& draft);
	} // MaterialAssetFields
} // Engine
