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

	// manifest が記録した source(.cs) と asset のパスを照合して型候補を得る。
	// 候補が0なら DLL 未登録、複数なら曖昧（同名 .cs に複数クラス）として採用しない。
	const std::vector<const BehaviorTypeInfo*> candidates =
		BehaviorTypeRegistry::GetInstance().FindManagedBySourceFile(meta->assetPath);
	if (candidates.empty()) {
		return false;
	}
	if (candidates.size() > 1) {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ScriptAssetDragDrop: ambiguous script source '{}' ({} candidates). drag&dropを中止します。",
			meta->assetPath, candidates.size());
		return false;
	}

	outType.scriptTypeId = candidates.front()->scriptTypeId;
	outType.typeName = candidates.front()->name;
	return true;
}

bool Engine::ScriptAssetDragDrop::AcceptScriptAssetDrop(const EditorPanelContext& context,
	AssetID& outAssetID, ResolvedScriptType& outType) {

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

	outAssetID = assetPayload.assetID;
	return true;
}
