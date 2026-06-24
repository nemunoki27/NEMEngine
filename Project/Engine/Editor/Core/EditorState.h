#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Editor/Commands/Entity/EditorEntitySnapshot.h>
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Core/Foundation/Utility/Enum/DimensionType.h>

// c++
#include <vector>
#include <optional>
#include <string>

namespace Engine {

	// エンティティのレンダラ等から2D/3Dを判定する、確定できなければnullopt
	std::optional<Dimension> ResolveEntityDimension(ECSWorld& world, const Entity& entity);

	// エンティティの表示名を返す、NameComponentが無いか名前が空なら"Entity"を返す
	std::string GetEntityDisplayName(ECSWorld& world, const Entity& entity);

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
		Joint,
	};

	// 1つのSRT軸のスナップ設定、グリッド単位と絶対スナップの有無を持つ
	struct GridSnapAxis {

		// スナップ単位、移動なら距離、回転なら度、拡縮なら倍率
		float size = 1.0f;
		// trueなら結果を最寄りのグリッドへ強制する、falseならグリッド単位の移動量にする
		bool absolute = false;
	};

	// ギズモ操作のスナップ設定、SRTそれぞれを2D/3Dで分けて持つ
	struct EntitySnapSettings {

		// 2D
		GridSnapAxis translate2D{ 1.0f, false };
		GridSnapAxis rotate2D{ 15.0f, false };
		GridSnapAxis scale2D{ 0.2f, false };
		// 3D
		GridSnapAxis translate3D{ 1.0f, false };
		GridSnapAxis rotate3D{ 15.0f, false };
		GridSnapAxis scale3D{ 0.2f, false };

		// 選択中エンティティの次元に応じてスナップグリッド線を描画するか
		bool drawSnapGrid = false;
	};

	// DeferredのGBufferデバッグ表示、Noneなら通常描画、それ以外はそのバッファをViewへ表示する
	enum class GBufferDebugView :
		uint8_t {

		None,
		Albedo,
		Normal,
		Position,
		Material,
		Emissive,
		Depth,
	};

	// エディタの状態を管理する構造体
	struct EditorState {

		// 現在選択しているエンティティ、複数選択時はアクティブな1件
		Entity selectedEntity = Entity::Null();
		// 複数選択しているエンティティ一覧、selectedEntityもこの中に含む
		std::vector<Entity> selectedEntities{};
		// Ctrl+ドラッグ用に選択を変えずカーソル下から拾ったエンティティ、Viewportのドラッグ対象に使う
		Entity scenePickDragEntity = Entity::Null();
		// ダブルクリックでシーンカメラを寄せたいエンティティ、EditorManagerが消費する
		Entity cameraFocusRequest = Entity::Null();
		// シーンカメラがフォーカスで寄っている最中か、フォーカス中はギズモ操作を無効にする
		bool cameraFocusing = false;
		// 現在選択しているアセット
		AssetID selectedAsset{};
		// アセット選択操作が行われるたびに進むカウンタ
		uint64_t assetSelectionRevision = 0;

		// 現在選択しているオブジェクトの種類
		EditorSelectionKind selectionKind = EditorSelectionKind::None;
		uint32_t selectedSubMeshIndex = 0;
		// サブメッシュの永続選択ID
		UUID selectedSubMeshStableID{};

		// 現在の選択の種類
		EditorSelectionKind selectKind = EditorSelectionKind::Entity;

		// 選択中ジョイント、スキンメッシュエンティティとジョイントindex
		Entity selectedJointSkinnedEntity = Entity::Null();
		int32_t selectedJointIndex = -1;

		// Undo / Redo履歴
		EditorCommandHistory commandHistory{};
		// Copy / Paste用クリップボード、複数選択をまとめて保持する
		EditorEntityTreeSnapshot clipboardSnapshot{};
		UUID clipboardParentStableUUID{};
		std::vector<EditorEntityTreeSnapshot> clipboardSnapshots{};
		std::vector<UUID> clipboardParentUUIDs{};

		// シーンビューのカメラ選択状態
		SceneViewCameraSelection sceneViewCamera{};
		// 使用しているカメラの次元
		Dimension manualCameraDimension = Dimension::Type3D;

		// ピッキング機能のオン/オフ
		bool enableScenePick = true;
		// SceneView/GameViewのImageが最前面でホバーされているか、ViewportPanelが毎フレーム更新する
		// 他のImGuiウィンドウやポップアップが上にある時はfalseになり、ピッキングを抑止する
		bool sceneViewportHovered = false;
		bool gameViewportHovered = false;
		// SceneViewのデフォルトグリッド表示
		bool drawSceneViewDefaultGrid = false;
		// シーンビューのマニピュレーター選択状態
		SceneViewManipulatorMode sceneViewManipulatorMode = SceneViewManipulatorMode::Translate;
		// ギズモを使用中か
		bool useSceneGizmo = false;
		// スナップ操作の有効/無効
		bool enableSnapEditEntity = false;
		// ギズモのスナップ設定、シリアライズ対象
		EntitySnapSettings snapSettings{};
		// アセットをSceneViewへスナップ有効でドラッグ中か、スナップグリッド表示のためViewportPanelが毎フレーム更新する
		bool assetDragSnapGridActive = false;
		// ドラッグ中アセットが3Dか、グリッドの2D/3Dをドラッグ対象の次元に切り替えるのに使う
		bool assetDragSnapGridIs3D = false;
		// 複数選択ギズモの回転拡縮を選択中心基準で行うか、falseなら各エンティティ自身の原点基準
		bool gizmoPivotAtCenter = true;

		// GBufferデバッグ表示、Noneなら通常描画、選べるのは常に1つだけ
		GBufferDebugView gbufferDebugView = GBufferDebugView::None;

		// 選択しているエンティティがワールドに存在するか確認し、存在しない場合は選択をクリアする
		void ValidateSelection(ECSWorld* world);

		// 複数選択操作、追加トグルや範囲指定での置き換え
		void AddEntityToSelection(const Entity& entity);
		void ToggleEntityInSelection(const Entity& entity);
		void SetSelectedEntities(const std::vector<Entity>& entities);
		const std::vector<Entity>& GetSelectedEntities() const { return selectedEntities; }
		size_t SelectionCount() const { return selectedEntities.size(); }
		// 次元が一致して複数選択へ追加できるか、Textはdimensionで2D/3Dを切り替える
		bool CanMultiSelect(ECSWorld& world, const Entity& candidate) const;

		// エンティティやサブメッシュを選択する
		void SelectEntity(const Entity& entity);
		void SelectMeshSubMesh(const Entity& entity, uint32_t subMeshIndex, UUID stableID = UUID{});
		// スキンメッシュのジョイントを選択する
		void SelectJoint(const Entity& skinnedEntity, int32_t jointIndex);
		// 現在ジョイントが選択されているか、有効なら情報表示する
		bool HasValidJointSelection(ECSWorld* world) const;
		// 特定のジョイントが選択されているか
		bool IsJointSelected(const Entity& skinnedEntity, int32_t jointIndex) const;
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
		bool HasClipboard() const { return !clipboardSnapshots.empty(); }
	};
} // Engine
