#include "ScriptAssetDragDrop.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetDatabase.h>
#include <Engine/Core/Scripting/ManagedScriptRuntime.h>
#include <Engine/Editor/Panel/Interface/IEditorPanel.h>

// c++
#include <filesystem>

//============================================================================
//	ScriptAssetDragDrop classMethods
//============================================================================

namespace {

	// ProjectパネルのPayloadをScriptアセットとして読み取る
	bool TryReadScriptPayload(const ImGuiPayload* payload, Engine::EditorAssetDragDropPayload& outPayload) {

		if (!payload || payload->DataSize != sizeof(Engine::EditorAssetDragDropPayload)) {
			return false;
		}

		outPayload = *static_cast<const Engine::EditorAssetDragDropPayload*>(payload->Data);
		return outPayload.assetType == Engine::AssetType::Script;
	}
}

bool Engine::ScriptAssetDragDrop::ResolveScriptTypeName(const EditorPanelContext& context,
	AssetID assetID, std::string& outTypeName) {

	if (!assetID || !context.editorContext || !context.editorContext->assetDatabase) {
		return false;
	}

	const AssetMeta* meta = context.editorContext->assetDatabase->Find(assetID);
	if (!meta || meta->type != AssetType::Script) {
		return false;
	}

	// Scriptアセットはファイル名をC#クラス名として解決する
	const std::string scriptName = std::filesystem::path(meta->assetPath).stem().string();
	return ManagedScriptRuntime::GetInstance().TryResolveScriptTypeName(scriptName, outTypeName);
}

bool Engine::ScriptAssetDragDrop::AcceptScriptAssetDrop(const EditorPanelContext& context,
	AssetID& outAssetID, std::string& outTypeName) {

	const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kProjectAssetDragDropPayloadType);
	if (!payload || !payload->IsDelivery()) {
		return false;
	}

	EditorAssetDragDropPayload assetPayload{};
	if (!TryReadScriptPayload(payload, assetPayload)) {
		return false;
	}

	if (!ResolveScriptTypeName(context, assetPayload.assetID, outTypeName)) {
		return false;
	}

	outAssetID = assetPayload.assetID;
	return true;
}
