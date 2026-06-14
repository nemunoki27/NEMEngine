#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>

namespace Engine {

	// front
	struct ShaderReflectionInfo;

	//============================================================================
	//	MeshRendererInspectorDrawer class
	//	メッシュレンダラーコンポーネントのインスペクター描画
	//============================================================================
	class MeshRendererInspectorDrawer :
		public SerializedComponentInspectorDrawer<MeshRendererComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		MeshRendererInspectorDrawer() :
			SerializedComponentInspectorDrawer("Mesh Renderer", "MeshRenderer") {
		}
		~MeshRendererInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// メッシュアセットのキャッシュ
		AssetID cachedMeshAssetID_{};
		std::vector<MeshSubMeshLayoutItem> cachedSubMeshLayout_{};
		bool cachedSubMeshLayoutResolved_ = false;

		// マテリアル既定値とreflection解決のためのキャッシュ
		AssetID cachedMaterialID_{};
		MaterialAsset cachedMaterial_{};
		bool cachedMaterialValid_ = false;

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;

		// プレビュー適用時にランタイム行列も更新する
		void ApplyPreview(ECSWorld& world, const Entity& entity,
			const MeshRendererComponent& previewComponent) override;

		// メッシュアセットからサブメッシュ名キャッシュを更新する
		void RefreshSubMeshLayoutCache(AssetDatabase* assetDatabase, AssetID meshAssetID);
		// ドラフトのサブメッシュリストをワールドの内容と同期する
		void SyncDraftSubMeshes(const EditorPanelContext& context, MeshRendererComponent& draft, bool preserveOverrides);
		// サブメッシュの選択状態をワールドの内容と照らし合わせて確認する
		bool TryGetSelectedSubMeshIndex(const EditorPanelContext& context,
			ECSWorld& world, const Entity& entity, const MeshRendererComponent& draft,
			uint32_t& outSubMeshIndex) const;
		// サブメッシュのフィールドを描画する
		void DrawSubMeshFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, SubMeshMaterial& subMesh, bool& anyItemActive);
		// マテリアルのDrawパスreflectionを解決しキャッシュする、失敗時はnullptr
		const ShaderReflectionInfo* EnsureMaterialReflection(const EditorPanelContext& context, AssetID materialID);
		// シェーダーreflection駆動でサブメッシュ単位のマテリアルパラメータを編集する
		void DrawSubMeshReflectedParameters(const EditorPanelContext& context,
			AssetID materialID, SubMeshMaterial& subMesh, bool& anyItemActive);
		// ドラフトの内容をワールドのコンポーネントに反映する前の追加処理
		void UpdateDraftRuntime(ECSWorld& world, const Entity& entity,
			MeshRendererComponent& draft) const;
	};
} // Engine

