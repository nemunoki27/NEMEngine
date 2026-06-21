#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/SkyboxRendererComponent.h>

namespace Engine {

	//============================================================================
	//	SkyboxRendererInspectorDrawer class
	//	スカイボックスレンダラーコンポーネントのインスペクター描画
	//============================================================================
	class SkyboxRendererInspectorDrawer :
		public SerializedComponentInspectorDrawer<SkyboxRendererComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SkyboxRendererInspectorDrawer() :
			SerializedComponentInspectorDrawer("Skybox Renderer", "SkyboxRenderer") {
		}
		~SkyboxRendererInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};
} // Engine
