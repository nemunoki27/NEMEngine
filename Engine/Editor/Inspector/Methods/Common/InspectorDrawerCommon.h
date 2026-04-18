#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/ImGui/MyGUI.h>
#include <Engine/Core/Asset/AssetTypes.h>
#include <Engine/Core/Asset/AssetDatabase.h>
#include <Engine/Core/ECS/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/UUID/UUID.h>

// c++
#include <initializer_list>

//============================================================================
//	InspectorDrawerCommon namespace
//	インスペクターの共通描画関数
//============================================================================
namespace Engine::InspectorDrawerCommon {

	// 値編集の結果を累積する
	void AccumulateEditResult(const ValueEditResult& result, bool& anyItemActive, bool& commitRequested);

	// チェックボックスフィールドを描画する
	ValueEditResult DrawCheckboxField(const char* label, bool& value);
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
	// ビヘイビアの型選択フィールドを描画する
	ValueEditResult DrawBehaviorTypeField(const char* label, std::string& type);

	// エンティティの種類に応じてデバッグラインを描画する
	void DrawEntityDebugObject(ECSWorld& world, const Entity& entity);
}