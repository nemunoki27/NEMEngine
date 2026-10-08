#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>

namespace Engine {

	class ECSWorld;
	struct EditorPanelContext;

	//============================================================================
	//	InspectorSelectionDisplay namespace
	//	SubMeshとJointの選択情報を表示する
	//============================================================================
	namespace InspectorSelectionDisplay {

		// 選択SubMeshの名前と所有Entityを表示する
		void DrawSelectedSubMeshHeader(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// 選択Jointの親とワールド行列を表示する
		void DrawJointInspector(const EditorPanelContext& context);
	} // InspectorSelectionDisplay
} // Engine
