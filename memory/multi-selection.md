---
name: multi-selection
description: エンティティ複数選択機能の実装状況(コア完了、エリア選択のみ次回)
metadata:
  type: project
---

2026-06-14 実装(build+45s smoke、実機GUIは未確認). エディタの単一選択を複数選択へ拡張.

**DONE:**
- `EditorState`(Editor/Core): `selectedEntity`=アクティブ1件は維持、`selectedEntities`(vector)に全選択. `AddEntityToSelection`/`ToggleEntityInSelection`/`SetSelectedEntities`/`CanMultiSelect`(次元ガード)/`GetSelectedEntities`/`SelectionCount`. `IsEntitySelected`はset判定でハイライト自動対応. `ValidateSelection`は死亡個体をprune+activeを生存へ. クリップボードを`clipboardSnapshots`/`clipboardParentUUIDs`(vector)へ. 次元判定は`ResolveEntityDimension`(anon、Sprite/Ortho=2D、Mesh/Persp/Light=3D、**Textはdimensionで2D/3D切替**、無renderer=neutral).
- 入力(Shift=左シフトでBlender風トグル追加、同次元のみ): SceneView/GameViewのpickは`EditorManager::ExecuteSceneMeshPicking`の`executePick`(2D/overlay同期は`selectHit`ラムダ、mesh非同期は`MeshSubMeshPicker`に`pendingAdditive_`を持たせ`ConsumePendingResult`でトグル). 通常クリックは置換、空クリックは解除(非additive). HierarchyPanelはノード/チェックボックスクリックを`selectEntityInHierarchy`でトグル化、右クリックは選択済みなら維持.
- 操作(複数): Delete/Duplicate(EditorManager+Hierarchy contextでループ)、Copy/Paste(snapshot list)、有効無効(Hierarchyの目アイコン+contextメニューが選択中なら全選択へ同状態`SetEntityActiveCommand`).
- ギズモ複数(`ViewportPanel::DrawMultiEntityGizmo`+`MultiEntityGizmoSession`): 選択2件以上で中心ピボット、**移動=共通world delta、回転/拡縮=各エンティティ自身の原点基準(個別)**、フレーム差分を`ApplyImmediate`で適用、session終了で各`SetTransformCommand`(複数undo). 単一選択は従来パス. `Prefers2DGizmo`もText dimension対応へ修正済.

**追加修正(2026-06-14):**
- ギズモ確定後に複数選択が解除される件: `SetTransformCommand`(:51-56)が非アクティブ対象を`SelectEntity`で単一化するのが原因. `FinalizeMultiEntityGizmoSession`でコマンド群の前後に選択を退避し`SetSelectedEntities`で復元.
- **全選択にアウトライン**: アウトライン要求は`InspectorDrawerCommon::DrawEntityDebugObject`→`EditorSelectionOutlineRequestService`(v8でvector対応済). `EditorManager`(:802付近)の呼び出しを`GetSelectedEntities`でループ化、複数時は各エンティティ全体(subMesh=-1)、アクティブのみsubMesh番号反映.

**追加(2026-06-14 その2):**
- 複製/ペースト後に全複製分を選択維持: `DuplicateSelection`/`PasteClipboard`が各コマンド実行後の`selectedEntity`(=新ルート)を集約し`SetSelectedEntities`で複数選択へ.
- ギズモのピボットモード切替: `EditorState.gizmoPivotAtCenter`(**既定true=選択中心Blender風**, false=各原点). `DrawMultiEntityGizmo`が分岐(center時は位置を`RotateVectorByQuaternion`でorbit×scale、移動先適用後に再計算で二重適用回避). ViewportPanelのDrawManipulatorSectionで選択対象切替の上にImageButton(暫定で`entitySelectKey`流用、専用iconは未提供なので差し替え可).

**未実装:** エリア選択はユーザー判断で不要(shift選択で十分).

**割り切り:** ギズモ移動の親space変換は未対応(localPos加算、上位選択前提). Inspectorの共通コンポーネント同時編集はユーザー指示で実装しない.
