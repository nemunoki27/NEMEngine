#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>

namespace Engine {

	//============================================================================
	//	CollisionInspectorDrawer class
	//	Collisionコンポーネントのインスペクター描画
	//============================================================================
	class CollisionInspectorDrawer :
		public SerializedComponentInspectorDrawer<CollisionComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		CollisionInspectorDrawer() :
			SerializedComponentInspectorDrawer("Collision", "Collision") {
		}
		~CollisionInspectorDrawer() override = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 編集中の衝突形状一覧
		std::vector<CollisionShape> shapeDraft_;

		//--------- functions ----------------------------------------------------

		// Collisionコンポーネントの編集項目を描画する
		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
		// ワールドから衝突形状一覧を同期する
		void OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity,
			const CollisionComponent& component) override;
		// 衝突形状一覧を含むドラフトをjsonへ変換する
		void SerializeDraft(ECSWorld& world, const Entity& entity,
			const CollisionComponent& component, nlohmann::json& out) const override;
		// プレビューをワールドへ適用する
		void ApplyPreview(ECSWorld& world, const Entity& entity,
			const CollisionComponent& previewComponent) override;

		// Collisionタイプマスクの編集UIを描画する
		ValueEditResult DrawTypeMaskField(CollisionComponent& component);
		// 衝突形状タイプの編集UIを描画する
		ValueEditResult DrawShapeTypeField(ColliderShapeType& type);
		// 衝突形状の詳細編集UIを描画する
		ValueEditResult DrawShapeField(CollisionShape& shape, uint32_t index);
	};
} // Engine

