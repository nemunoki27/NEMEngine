#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>

// c++
#include <initializer_list>
#include <utility>

//============================================================================
//	InspectorDrawerCommon namespace
//	インスペクターの共通描画関数
//============================================================================
namespace Engine::InspectorDrawerCommon {

	// 値編集の結果を累積する
	void AccumulateEditResult(const ValueEditResult& result, bool& anyItemActive, bool& commitRequested);

	// チェックボックスフィールドを描画する
	ValueEditResult DrawCheckboxField(const char* label, bool& value);
	// uint32のレイヤーマスクをDragIntで描画する、内部はint32経由で編集する
	ValueEditResult DrawLayerMaskField(const char* label, uint32_t& value);
	// enum型のコンボボックスフィールドを描画する
	template <typename Enum>
	ValueEditResult DrawEnumComboField(const char* label, Enum& value) {

		ValueEditResult result{};
		if (!MyGUI::BeginPropertyRow(label)) {
			return result;
		}

		result.valueChanged = EnumAdapter<Enum>::Combo("##Value", &value);
		result.anyItemActive = ImGui::IsItemActive();
		result.editFinished = result.valueChanged || ImGui::IsItemDeactivatedAfterEdit();

		MyGUI::EndPropertyRow();
		return result;
	}
	// ビヘイビアの型選択フィールドを描画する、searchIconはcombo内検索欄に重ねる虫眼鏡
	ValueEditResult DrawBehaviorTypeField(const char* label, std::string& type, ImTextureID searchIcon);

	// 各Rendererコンポーネントが共通で持つ描画フィールドを描く、drawFieldは各Drawerのラップを渡す
	template <typename DrawFieldFn>
	void DrawCommonRenderFields(DrawFieldFn&& drawField,
		int32_t& layer, int32_t& order, bool& visible, BlendMode& blendMode,
		RenderPhase& queue, uint32_t* renderingLayerMask = nullptr) {

		drawField([&]() { return MyGUI::DragInt("レイヤー", layer); });
		drawField([&]() { return MyGUI::DragInt("描画順", order); });
		drawField([&]() { return DrawCheckboxField("表示", visible); });
		drawField([&]() { return DrawEnumComboField("ブレンドモード", blendMode); });
		drawField([&]() { return DrawEnumComboField("キュー", queue); });
		if (renderingLayerMask) {
			drawField([&]() {

				return DrawLayerMaskField(
					"Rendering Layer", *renderingLayerMask);
			});
		}
	}

	// エンティティの種類に応じてデバッグラインを描画する
	void DrawEntityDebugObject(ECSWorld& world, const Entity& entity, int32_t selectionSubMeshIndex = -1);
}
