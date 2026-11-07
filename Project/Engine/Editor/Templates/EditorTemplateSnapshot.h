#pragma once

//============================================================================
//	include
//============================================================================
#include "EditorTemplateTypes.h"

namespace Engine {

	class AssetDatabase;

	// 保存済みシーンから編集可能な配布元を作成する
	EditorTemplateResult SaveEditorTemplate(const EditorTemplateSaveRequest& request, const AssetDatabase& database);
}
