#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/CameraControllerComponent.h>
#include <Engine/Editor/Inspector/Methods/Common/SerializedComponentInspectorDrawer.h>

namespace Engine {

	//============================================================================
	//	CameraControllerInspectorDrawer class
	//	カメラ制御コンポーネントのインスペクター描画
	//============================================================================
	class CameraControllerInspectorDrawer :
		public SerializedComponentInspectorDrawer<CameraControllerComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		CameraControllerInspectorDrawer() :
			SerializedComponentInspectorDrawer("CameraController", "CameraController") {
		}
		~CameraControllerInspectorDrawer() = default;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
		void OnBeforeCommit(const CameraControllerComponent& beforeComponent, CameraControllerComponent& afterComponent) override;

		// 制御方法を描画する
		ValueEditResult DrawModeField(CameraControlMode& mode);
		// Entity参照を描画する
		ValueEditResult DrawEntityTargetField(const char* label, ECSWorld& world, UUID& target);
		// 追従設定を描画する
		ValueEditResult DrawFollowSettings(ECSWorld& world, CameraFollowSettings& settings);
		// 注視設定を描画する
		ValueEditResult DrawLookAtSettings(ECSWorld& world, CameraLookAtSettings& settings);
		// 揺れ設定を描画する
		ValueEditResult DrawShakeSettings(CameraShakeSettings& settings);
	};
} // Engine
