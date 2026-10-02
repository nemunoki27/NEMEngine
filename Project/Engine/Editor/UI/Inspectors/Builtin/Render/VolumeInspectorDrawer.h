#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/VolumeComponent.h>

namespace Engine {

	//============================================================================
	//	VolumeInspectorDrawer class
	//	Volume設定を編集するインスペクター
	//============================================================================
	class VolumeInspectorDrawer final :
		public SerializedComponentInspectorDrawer<VolumeComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		VolumeInspectorDrawer() : SerializedComponentInspectorDrawer("Volume", "Volume") {}
		~VolumeInspectorDrawer() override = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		// Volume設定を描画する
		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
		// 数値範囲を確定する
		void OnBeforeCommit(const VolumeComponent& beforeComponent, VolumeComponent& afterComponent) override;
	};
} // Engine
