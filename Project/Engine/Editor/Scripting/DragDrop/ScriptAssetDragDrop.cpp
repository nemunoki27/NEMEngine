#include "ScriptAssetDragDrop.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>

//============================================================================
//	ScriptAssetDragDrop classMethods
//============================================================================
namespace {

	// ProjectパネルのPayloadをScriptアセットとして読み取る
	bool TryReadScriptPayload(const ImGuiPayload* payload, Engine::EditorAssetDragDropPayload& outPayload) {

		if (!payload || payload->DataSize != sizeof(Engine::EditorAssetDragDropPayload)) {
			return false;
		}

		// 検証済みPayloadをコピー
		outPayload = *static_cast<const Engine::EditorAssetDragDropPayload*>(payload->Data);
		return outPayload.assetType == Engine::AssetType::Script;
	}
}

bool Engine::ScriptAssetDragDrop::ResolveScriptType(const EditorPanelContext& context,
	AssetID assetID, ResolvedScriptType& outType) {

	if (!assetID || !context.editorContext || !context.editorContext->assetDatabase) {
		return false;
	}

	const AssetMeta* meta = context.editorContext->assetDatabase->Find(assetID);
	if (!meta || meta->type != AssetType::Script) {
		return false;
	}

	// Scriptのファイル名で登録済み型を照合
	const std::vector<const BehaviorTypeInfo*> candidates =
		BehaviorTypeRegistry::GetInstance().FindManagedBySourceFile(meta->assetPath);
	if (candidates.empty()) {
		return false;
	}
	if (candidates.size() > 1) {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ScriptAssetDragDrop: Script候補を一意に決められないためDrag&Dropを中止します path={} 候補数={}",
			meta->assetPath, candidates.size());
		return false;
	}

	// 一意な型の識別子と名前をコピー
	outType.scriptTypeID = candidates.front()->scriptTypeID;
	outType.typeName = candidates.front()->name;
	return true;
}

bool Engine::ScriptAssetDragDrop::AcceptScriptAssetDrop(const EditorPanelContext& context,
	AssetID& outAssetID, ResolvedScriptType& outType) {

	// ドロップ確定時だけScriptを受け取る
	const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kProjectAssetDragDropPayloadType);
	if (!payload || !payload->IsDelivery()) {
		return false;
	}

	EditorAssetDragDropPayload assetPayload{};
	if (!TryReadScriptPayload(payload, assetPayload)) {
		return false;
	}

	if (!ResolveScriptType(context, assetPayload.assetID, outType)) {
		return false;
	}

	// 解決済みScriptのAssetを返す
	outAssetID = assetPayload.assetID;
	return true;
}
