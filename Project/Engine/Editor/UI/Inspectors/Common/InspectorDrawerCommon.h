#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiEnum.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>

// c++
#include <initializer_list>
#include <utility>

//============================================================================
//	InspectorDrawerCommon namespace
//	インスペクターの共通描画関数
//============================================================================
namespace Engine::InspectorDrawerCommon {

	// 描画と同じ索引から既定Materialを選ぶ
	AssetID ResolveDefaultMaterial(const EditorPanelContext& context, DefaultMaterialSlot slot);

	// 値編集の結果を累積する
	void AccumulateEditResult(const ValueEditResult& result, bool& anyItemActive, bool& commitRequested);

	// フィールドを表示して編集結果をまとめる
	template <typename DrawFunc>
	void DrawFieldEdit(ValueEditResult& accumulated, DrawFunc&& drawFunc) {

		const ValueEditResult result = std::forward<DrawFunc>(drawFunc)();
		AccumulateEditResult(result, accumulated.anyItemActive, accumulated.editFinished);
		accumulated.valueChanged |= result.valueChanged;
		ImGui::Separator();
	}

	// チェックボックスフィールドを描画する
	ValueEditResult DrawCheckboxField(const char* label, bool& value);
	// 定義済みのRendering Layerを選択する
	ValueEditResult DrawLayerMaskField(const EditorPanelContext& context, const char* label, uint32_t& value);
	// 列挙型の選択欄を描画する
	template <typename Enum>
	ValueEditResult DrawEnumComboField(const char* label, Enum& value) {

		ValueEditResult result{};
		if (!MyGUI::BeginPropertyRow(label)) {
			return result;
		}

		result.valueChanged = Engine::ImGuiUtility::EnumCombo<Enum>("##Value", &value);
		result.anyItemActive = ImGui::IsItemActive();
		result.editFinished = result.valueChanged || ImGui::IsItemDeactivatedAfterEdit();

		MyGUI::EndPropertyRow();
		return result;
	}
	// 検索付きのBehavior型選択欄を描画する
	ValueEditResult DrawBehaviorTypeField(const char* label, std::string& type, ImTextureID searchIcon);

	// Rendererの共通設定をDrawerへ渡す
	template <typename DrawFieldFn>
	void DrawCommonRenderFields(const EditorPanelContext& context, DrawFieldFn&& drawField,
		int32_t& layer, int32_t& order, BlendMode& blendMode,
		RenderPhase& queue, uint32_t* renderingLayerMask = nullptr) {

		drawField([&]() { return MyGUI::DragInt("レイヤー", layer); });
		drawField([&]() { return MyGUI::DragInt("描画順", order); });
		drawField([&]() { return DrawEnumComboField("ブレンドモード", blendMode); });
		drawField([&]() { return DrawEnumComboField("キュー", queue); });
		if (renderingLayerMask) {
			drawField([&]() {

				return DrawLayerMaskField(context, "Rendering Layer", *renderingLayerMask);
			});
		}
	}

	// エンティティの種類に応じてデバッグラインを描画する
	void DrawEntityDebugObject(ECSWorld& world, const Entity& entity, int32_t selectionSubMeshIndex = -1);
}
