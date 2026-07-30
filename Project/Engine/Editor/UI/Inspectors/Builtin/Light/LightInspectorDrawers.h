#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Lighting/RectLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SpotLightComponent.h>

namespace Engine {

	//============================================================================
	//	DirectionalLightInspectorDrawer class
	//	平行光源コンポーネントのインスペクター描画
	//============================================================================
	class DirectionalLightInspectorDrawer :
		public SerializedComponentInspectorDrawer<DirectionalLightComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		DirectionalLightInspectorDrawer() :
			SerializedComponentInspectorDrawer("DirectionalLight", "DirectionalLight") {}
		~DirectionalLightInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
		void OnBeforeCommit(const DirectionalLightComponent& beforeComponent,
			DirectionalLightComponent& afterComponent) override;
	};

	//============================================================================
	//	PointLightInspectorDrawer class
	//	点光源コンポーネントのインスペクター描画
	//============================================================================
	class PointLightInspectorDrawer :
		public SerializedComponentInspectorDrawer<PointLightComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		PointLightInspectorDrawer() :
			SerializedComponentInspectorDrawer("PointLight", "PointLight") {}
		~PointLightInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};

	//============================================================================
	//	RectLightInspectorDrawer class
	//	矩形面光源コンポーネントのインスペクター描画
	//============================================================================
	class RectLightInspectorDrawer :
		public SerializedComponentInspectorDrawer<RectLightComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		RectLightInspectorDrawer() :
			SerializedComponentInspectorDrawer("RectLight", "RectLight") {}
		~RectLightInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};

	//============================================================================
	//	SpotLightInspectorDrawer class
	//	スポットライトコンポーネントのインスペクター描画
	//============================================================================
	class SpotLightInspectorDrawer :
		public SerializedComponentInspectorDrawer<SpotLightComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SpotLightInspectorDrawer() :
			SerializedComponentInspectorDrawer("SpotLight", "SpotLight") {}
		~SpotLightInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
		void OnBeforeCommit(const SpotLightComponent& beforeComponent,
			SpotLightComponent& afterComponent) override;
	};
} // Engine
