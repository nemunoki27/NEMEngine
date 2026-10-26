#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/ParticlePaintReceiverComponent.h>

namespace Engine {

	// 塗装を受け取るTextureと投影範囲を編集する
	class ParticlePaintReceiverInspectorDrawer : public SerializedComponentInspectorDrawer<ParticlePaintReceiverComponent> {
	public:

		ParticlePaintReceiverInspectorDrawer() : SerializedComponentInspectorDrawer("ParticlePaintReceiver", "ParticlePaintReceiver") {}
	private:

		// 保存する設定と実行時の消去操作を表示する
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
	};
}
