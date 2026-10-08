#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Editor/UI/Inspectors/Common/MaterialReflectionCache.h>

namespace Engine {

	// front
	struct ShaderReflectionInfo;
	struct ShaderConstantBufferVariable;

	//============================================================================
	//	PrimitiveRendererInspectorDrawer class
	//	PrimitiveRendererComponentのインスペクター描画
	//============================================================================
	class PrimitiveRendererInspectorDrawer :
		public SerializedComponentInspectorDrawer<PrimitiveRendererComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		PrimitiveRendererInspectorDrawer() : SerializedComponentInspectorDrawer("Primitive Renderer", "PrimitiveRenderer") {}
		~PrimitiveRendererInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// マテリアル既定値とreflection解決のためのキャッシュ
		MaterialReflectionCache materialReflection_;

		//--------- functions ----------------------------------------------------

		// Componentの編集項目を表示する
		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;

		// シェーダーreflection駆動でマテリアルパラメータを編集する
		void DrawReflectedParameters(const EditorPanelContext& context, PrimitiveRendererComponent& draft, bool& anyItemActive);
	};
} // Engine
