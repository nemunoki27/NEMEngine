#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include "MeshMaterialEditSession.h"

// c++
#include <string>
#include <memory>

namespace Engine {

	//============================================================================
	//	MeshRendererInspectorDrawer class
	//	メッシュレンダラーコンポーネントのインスペクター描画
	//============================================================================
	class MeshRendererInspectorDrawer : public SerializedComponentInspectorDrawer<MeshRendererComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		MeshRendererInspectorDrawer() : SerializedComponentInspectorDrawer("Mesh Renderer", "MeshRenderer") {}
		~MeshRendererInspectorDrawer() = default;

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// メッシュアセットのキャッシュ
		AssetID cachedMeshAssetID_{};
		// Projectのcache世代
		std::weak_ptr<const uint8_t> cachedDatabaseLifetime_;
		// Asset索引の変更版
		uint64_t cachedDatabaseRevision_ = 0;
		// Mesh内容の変更版
		uint64_t cachedMeshContentRevision_ = 0;
		// 解決済みのSubMesh配置
		std::vector<MeshSubMeshLayoutItem> cachedSubMeshLayout_{};
		// 配置解決の成否
		bool cachedSubMeshLayoutResolved_ = false;

		// Materialの表示と同時編集の状態
		MeshMaterialEditSession materialEditor_;
		// DynamicBufferから分離した編集中のサブメッシュ一覧
		std::vector<SubMeshMaterial> subMeshDraft_{};
		// 単一サブメッシュ編集中はDynamicBuffer全体の再構築を避ける
		uint32_t previewSubMeshIndex_ = UINT32_MAX;

		//--------- functions ----------------------------------------------------

		// Componentの編集項目を表示する
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
		// ワールドのDynamicBufferから編集用配列を同期する
		void OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity, const MeshRendererComponent& component) override;
		// サブメッシュ一覧を含むドラフトを保存データへ変換する
		void SerializeDraft(
			ECSWorld& world, const Entity& entity, const MeshRendererComponent& component, nlohmann::json& out) const override;

		// プレビュー適用時にランタイム行列も更新する
		void ApplyPreview(ECSWorld& world, const Entity& entity, const MeshRendererComponent& previewComponent) override;

		// メッシュアセットからサブメッシュ名キャッシュを更新する
		void RefreshSubMeshLayoutCache(AssetDatabase* assetDatabase, AssetID meshAssetID);
		// ドラフトのサブメッシュリストをワールドの内容と同期する
		void SyncDraftSubMeshes(const EditorPanelContext& context, MeshRendererComponent& draft, bool preserveOverrides);
		// サブメッシュの選択状態をワールドの内容と照らし合わせて確認する
		bool TryGetSelectedSubMeshIndex(const EditorPanelContext& context, ECSWorld& world, const Entity& entity,
			const MeshRendererComponent& draft, uint32_t& outSubMeshIndex) const;
		// サブメッシュのフィールドを描画する
		void DrawSubMeshFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity,
			SubMeshMaterial& subMesh, bool& anyItemActive);
		// モデルファイルのマテリアル係数とテクスチャを現shaderのmaterialInstanceへ再適用する
		void ApplyModelMaterialParameters(const EditorPanelContext& context, MeshRendererComponent& draft);
	};
} // Engine
