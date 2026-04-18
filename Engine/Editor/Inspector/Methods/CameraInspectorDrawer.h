#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Inspector/Methods/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/ECS/Component/Builtin/CameraComponent.h>

namespace Engine {

	//============================================================================
	//	OrthographicCameraInspectorDrawer class
	//	カメラコンポーネントのインスペクター描画
	//============================================================================
	class OrthographicCameraInspectorDrawer :
		public SerializedComponentInspectorDrawer<OrthographicCameraComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		OrthographicCameraInspectorDrawer() :
			SerializedComponentInspectorDrawer("OrthographicCamera", "OrthographicCamera") {
		}
		~OrthographicCameraInspectorDrawer() = default;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
		void OnBeforeCommit(const OrthographicCameraComponent& beforeComponent, OrthographicCameraComponent& afterComponent) override;
	};

	//============================================================================
	//	PerspectiveCameraInspectorDrawer class
	//	カメラコンポーネントのインスペクター描画
	//============================================================================
	class PerspectiveCameraInspectorDrawer :
		public SerializedComponentInspectorDrawer<PerspectiveCameraComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PerspectiveCameraInspectorDrawer() :
			SerializedComponentInspectorDrawer("PerspectiveCamera", "PerspectiveCamera") {
		}
		~PerspectiveCameraInspectorDrawer() = default;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
		void OnBeforeCommit(const PerspectiveCameraComponent& beforeComponent, PerspectiveCameraComponent& afterComponent) override;
	};
} // Engine