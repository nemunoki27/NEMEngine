#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/CollisionComponent.h>
#include <Engine/Editor/Inspector/Methods/Common/SerializedComponentInspectorDrawer.h>

namespace Engine {

	//============================================================================
	//	CollisionInspectorDrawer class
	//	Collisionコンポーネントのインスペクター描画
	//============================================================================
	class CollisionInspectorDrawer :
		public SerializedComponentInspectorDrawer<CollisionComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		CollisionInspectorDrawer() :
			SerializedComponentInspectorDrawer("Collision", "Collision") {
		}
		~CollisionInspectorDrawer() override = default;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// Collisionコンポーネントの編集項目を描画する
		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;

		// Collisionタイプマスクの編集UIを描画する
		ValueEditResult DrawTypeMaskField(CollisionComponent& component);
		// 衝突形状タイプの編集UIを描画する
		ValueEditResult DrawShapeTypeField(ColliderShapeType& type);
		// 衝突形状の詳細編集UIを描画する
		ValueEditResult DrawShapeField(CollisionShape& shape, uint32_t index);
	};
} // Engine
