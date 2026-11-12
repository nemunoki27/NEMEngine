#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiEnum.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include "InspectorPresentation.h"
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
#include <string_view>
#include <utility>

//============================================================================
//	InspectorDrawerCommon namespace
//	インスペクターの共通描画関数
//============================================================================
namespace Engine::InspectorDrawerCommon {

	// 開発環境でのみコンポーネントの編集元を開く項目を描画する
	void DrawComponentSourceMenu(const std::string& sourceFile);
	// 登録名に対応するマニュアルへの項目を表示する
	void DrawComponentManualMenu(std::string_view component);

	// 描画と同じ索引から既定Materialを選ぶ
	AssetID ResolveDefaultMaterial(const EditorPanelContext& context, DefaultMaterialSlot slot);

	// 値編集の結果を累積する
	void AccumulateEditResult(const ValueEditResult& result, bool& anyItemActive, bool& commitRequested);

	// フィールドを表示して編集結果をまとめる
	template <typename DrawFunc>
	void DrawFieldEdit(ValueEditResult& accumulated, DrawFunc&& drawFunc) {

		accumulated.Merge(std::forward<DrawFunc>(drawFunc)());
		ImGui::Separator();
	}

	// チェックボックスフィールドを描画する
	ValueEditResult DrawCheckboxField(const char* label, bool& value, const PropertyRowSetting& setting = {});
	// 描画レイヤーの集合を選択する
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
	void DrawCommonRenderFields([[maybe_unused]] const EditorPanelContext& context, DrawFieldFn&& drawField,
		int32_t& layer, int32_t& order, BlendMode& blendMode,
		RenderPhase& queue) {

		drawField([&]() { return MyGUI::DragInt(Engine::GetInspectorLabel(Engine::InspectorFieldID::SortLayer), layer); });
		drawField([&]() { return MyGUI::DragInt(Engine::GetInspectorLabel(Engine::InspectorFieldID::DrawOrder), order); });
		drawField([&]() { return DrawEnumComboField(Engine::GetInspectorLabel(Engine::InspectorFieldID::BlendMode), blendMode); });
		drawField([&]() { return DrawEnumComboField(Engine::GetInspectorLabel(Engine::InspectorFieldID::RenderQueue), queue); });
	}

	// エンティティの種類に応じてデバッグラインを描画する
	void DrawEntityDebugObject(ECSWorld& world, const Entity& entity, int32_t selectionSubMeshIndex = -1);
}
