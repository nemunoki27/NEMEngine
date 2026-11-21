#pragma once

//============================================================================
//	include
//============================================================================
#include "EditorTemplateTypes.h"

namespace Engine {

	class AssetDatabase;

	// 保存済みシーンの依存アセットを一時フォルダーへ収集する
	EditorTemplateResult StageEditorTemplate(const EditorTemplateSaveRequest& request, const AssetDatabase& database);
	// 確認済みの保存元を入れ替える
	EditorTemplateResult CommitEditorTemplate(const EditorTemplateSaveRequest& request, const std::filesystem::path& staged);
	// 保存済みシーンから編集可能な配布元を作成する
	EditorTemplateResult SaveEditorTemplate(const EditorTemplateSaveRequest& request, const AssetDatabase& database);
}
