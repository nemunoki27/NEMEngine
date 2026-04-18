#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Inspector/Interface/IInspectorComponentDrawer.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/UUID/UUID.h>
#include <Engine/Utility/Enum/DimensionType.h>

namespace Engine {

	//============================================================================
	//	TransformInspectorDrawer class
	//	トランスフォームコンポーネントのインスペクター描画
	//============================================================================
	class TransformInspectorDrawer :
		public IInspectorComponentDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		TransformInspectorDrawer() = default;
		~TransformInspectorDrawer() = default;

		void Draw(const EditorPanelContext& context, ECSWorld& world, const Entity& entity) override;

		//--------- accessor -----------------------------------------------------

		bool CanDraw(ECSWorld& world, const Entity& entity) const override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 編集中のエンティティのUUID
		UUID editingEntityStableUUID_{};

		// ドラフトのトランスフォーム
		TransformComponent draftTransform_{};
		Vector3 draftEulerDegrees_{};

		// 編集状態か
		bool isEditing_ = false;
		// ドラフトの内容がワールドに反映されていない状態か
		bool commitRequested_ = false;

		// プレビューがアクティブか
		bool previewActive_ = false;
		bool previewRequested_ = false;
		TransformComponent previewBeginTransform_{};

		// 2Dモードか3Dモードか
		Dimension editDimension_ = Dimension::Type3D;

		//--------- functions ----------------------------------------------------

		// ドラフトをワールドから更新する
		void SyncDraftFromWorld(ECSWorld& world, const Entity& entity);
		// ドラフトをワールドに反映する
		void CommitTransformIfNeeded(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// プレビューが必要なら適用する
		void ApplyPreviewIfNeeded(ECSWorld& world, const Entity& entity);
	};
} // Engine