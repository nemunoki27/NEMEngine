#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Inspector/Methods/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h>
#include <Engine/Core/Graphics/Mesh/MeshSubMeshAuthoring.h>

namespace Engine {

	//============================================================================
	//	MeshRendererInspectorDrawer class
	//	メッシュレンダラーコンポーネントのインスペクター描画
	//============================================================================
	class MeshRendererInspectorDrawer :
		public SerializedComponentInspectorDrawer<MeshRendererComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		MeshRendererInspectorDrawer() :
			SerializedComponentInspectorDrawer("Mesh Renderer", "MeshRenderer") {
		}
		~MeshRendererInspectorDrawer() = default;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// メッシュアセットのキャッシュ
		AssetID cachedMeshAssetID_{};
		std::vector<MeshSubMeshLayoutItem> cachedSubMeshLayout_{};
		bool cachedSubMeshLayoutResolved_ = false;

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;

		// プレビュー適用時にランタイム行列も更新する
		void ApplyPreview(ECSWorld& world, const Entity& entity,
			const MeshRendererComponent& previewComponent) override;

		// メッシュアセットからサブメッシュ名キャッシュを更新する
		void RefreshSubMeshLayoutCache(const AssetDatabase* assetDatabase, AssetID meshAssetID);
		// ドラフトのサブメッシュリストをワールドの内容と同期する
		void SyncDraftSubMeshes(const EditorPanelContext& context, MeshRendererComponent& draft, bool preserveOverrides);
		// サブメッシュの選択状態をワールドの内容と照らし合わせて確認する
		bool TryGetSelectedSubMeshIndex(const EditorPanelContext& context,
			ECSWorld& world, const Entity& entity, const MeshRendererComponent& draft,
			uint32_t& outSubMeshIndex) const;
		// サブメッシュのフィールドを描画する
		void DrawSubMeshFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, MeshSubMeshTextureOverride& subMesh, bool& anyItemActive);
		// ドラフトの内容をワールドのコンポーネントに反映する前の追加処理
		void UpdateDraftRuntime(ECSWorld& world, const Entity& entity,
			MeshRendererComponent& draft) const;
	};
} // Engine