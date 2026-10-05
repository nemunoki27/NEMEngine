#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/UI/CanvasComponent.h>

namespace Engine {

	struct ValueEditResult;

	// Canvasの選択先と遷移表を編集する
	namespace CanvasNavigationEditing {

		// LocalFileIDで初期選択先を編集する
		ValueEditResult DrawEntityReference(const char* label, ECSWorld& world, UUID& localFileID);
		// 選択先の配置と入れ替えを表示する
		ValueEditResult DrawNavigationTable(ECSWorld& world, Entity canvas, CanvasNavigationTable& table);
	} // CanvasNavigationEditing
} // Engine
