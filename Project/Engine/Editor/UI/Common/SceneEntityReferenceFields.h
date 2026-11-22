#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/World/Scene/Utility/SceneEntityReference.h>

namespace Engine::SceneEntityReferenceFields {

	// 保存用の参照をHierarchyのEntityへ対応付けて編集する
	ValueEditResult Draw(const char* label, ECSWorld& world, Entity owner, SceneEntityReference& reference,
		const EntityEditSetting& setting = {}, UUID* runtimeReference = nullptr);
	// シーンのオブジェクトまたはアセットを指定する
	ValueEditResult DrawObjectOrAsset(const char* label, ECSWorld& world, Entity owner, SceneEntityReference& reference,
		UUID& runtimeReference, AssetID& asset, const AssetDatabase* database, const std::initializer_list<AssetType>& types,
		const EntityEditSetting& setting, const std::function<const char*(AssetID)>& validateAsset);
	// LocalFileIDだけを保持する既存の参照を編集する
	ValueEditResult DrawLocal(const char* label, ECSWorld& world, UUID& localFileID);
}
