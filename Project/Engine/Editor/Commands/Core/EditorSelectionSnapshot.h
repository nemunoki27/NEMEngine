#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorState.h>

namespace Engine {

	struct EditorCommandContext;

	//============================================================================
	//	EditorSelectionSnapshot class
	//	Undo前後の選択を実体IDで保持する
	//============================================================================
	class EditorSelectionSnapshot {
	public:
		// 選択種別と対象を保存する
		void Capture(const EditorCommandContext& context);
		// 同じWorld内で復元したEntityを引き直す
		bool Restore(const EditorCommandContext& context) const;
	private:
		struct SelectionEntity {

			Entity handle = Entity::Null();
			UUID stableUUID{};
		};

		static SelectionEntity CaptureEntity(ECSWorld* world, const Entity& entity);
		static Entity ResolveEntity(ECSWorld* world, const SelectionEntity& entity);

		EditorSelectionKind kind_ = EditorSelectionKind::None;
		std::vector<SelectionEntity> selected_;
		SelectionEntity active_;
		SelectionEntity joint_;
		AssetID asset_{};
		UUID subMeshID_{};
		uint32_t subMeshIndex_ = 0;
		int32_t jointIndex_ = -1;
		std::weak_ptr<const ECSWorldLifetime> worldLifetime_;
		bool worldBound_ = false;
	};
}
