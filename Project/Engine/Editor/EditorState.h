#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Entity/Entity.h>
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Editor/Command/EditorEntitySnapshot.h>
#include <Engine/Editor/Command/Interface/IEditorCommand.h>
#include <Engine/Utility/Enum/DimensionType.h>

namespace Engine {

	//============================================================================
	//	EditorState structures
	//============================================================================

	// シーンビューのカメラ選択モード
	enum class SceneViewCameraMode :
		uint8_t {

		DebugManual,
		SelectedEntityCamera,
	};

	// シーンビューのカメラ選択状態を管理する構造体
	struct SceneViewCameraSelection {

		SceneViewCameraMode mode = SceneViewCameraMode::DebugManual;

		// 2D/3DカメラのUUID
		UUID orthographicCameraUUID{};
		UUID perspectiveCameraUUID{};

		void ClearAssignedCameras();

		bool HasAnyAssignedCamera() const { return orthographicCameraUUID || perspectiveCameraUUID; }
	};

	// シーンビューのマニピュレーター選択モード
	enum class SceneViewManipulatorMode :
		uint8_t {

		None,
		Translate,
		Rotate,
		Scale,
	};
	// エディターの選択しているオブジェクトの種類
	enum class EditorSelectionKind :
		uint8_t {

		None,
		Entity,
		MeshSubMesh,
		Asset,
	};

	// エディタの状態を管理する構造体
	struct EditorState {

		// 現在選択しているエンティティ
		Entity selectedEntity = Entity::Null();
		// 現在選択しているアセット
		AssetID selectedAsset{};

		// 現在選択しているオブジェクトの種類
		EditorSelectionKind selectionKind = EditorSelectionKind::None;
		uint32_t selectedSubMeshIndex = 0;
		// サブメッシュの永続選択ID
		UUID selectedSubMeshStableID{};

		// 現在の選択の種類
		EditorSelectionKind selectKind = EditorSelectionKind::Entity;

		// Undo / Redo履歴
		EditorCommandHistory commandHistory{};
		// Copy / Paste用クリップボード
		EditorEntityTreeSnapshot clipboardSnapshot{};
		UUID clipboardParentStableUUID{};

		// シーンビューのカメラ選択状態
		SceneViewCameraSelection sceneViewCamera{};
		// 使用しているカメラの次元
		Dimension manualCameraDimension = Dimension::Type3D;

		// ピッキング機能のオン/オフ
		bool enableScenePick = true;
		// シーンビューのマニピュレーター選択状態
		SceneViewManipulatorMode sceneViewManipulatorMode = SceneViewManipulatorMode::None;
		// ギズモを使用中か
		bool useSceneGizmo = false;

		// 選択しているエンティティがワールドに存在するか確認し、存在しない場合は選択をクリアする
		void ValidateSelection(ECSWorld* world);

		// エンティティやサブメッシュを選択する
		void SelectEntity(const Entity& entity);
		void SelectMeshSubMesh(const Entity& entity, uint32_t subMeshIndex, UUID stableID = UUID{});
		void SelectAsset(AssetID asset);
		void SelectFromScenePick(const Entity& entity, uint32_t subMeshIndex, UUID stableID = UUID{});
		// 現在の選択がエンティティかサブメッシュか
		bool HasValidSubMeshSelection(ECSWorld* world) const;
		bool TryResolveSelectedSubMeshIndex(ECSWorld* world, uint32_t& outSubMeshIndex) const;
		// 現在の選択が特定のエンティティやサブメッシュか
		bool IsEntitySelected(const Entity& entity) const;
		bool IsMeshSubMeshSelected(const Entity& entity, UUID stableID, uint32_t subMeshIndex) const;
		bool IsAssetSelected(AssetID asset) const;

		// 選択をクリアする
		void ClearSelection();

		bool HasValidSelection(ECSWorld* world) const;
		bool HasClipboard() const { return !clipboardSnapshot.IsEmpty(); }
	};
} // Engine
