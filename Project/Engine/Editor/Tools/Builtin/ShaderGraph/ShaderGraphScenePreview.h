#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>

// c++
#include <memory>

namespace Engine {
	class ECSWorld;
	class ECSWorldLifetime;

	//============================================================================
	//	ShaderGraphScenePreview class
	//	シーンのMaterial差替えと復元状態を所有する
	//============================================================================
	class ShaderGraphScenePreview {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphScenePreview() = default;
		~ShaderGraphScenePreview();
		ShaderGraphScenePreview(const ShaderGraphScenePreview&) = delete;
		ShaderGraphScenePreview& operator=(const ShaderGraphScenePreview&) = delete;

		// Worldの切替と対象の削除を確認して復元する
		void SynchronizeWorld(ECSWorld* world);
		// 対象のMaterialをプレビューへ差し替える
		bool ApplyPreviewMaterial(const EditorToolContext& context, ShaderGraphTarget target,
			AssetID material, std::string& statusMessage);
		// 差替え前のMaterialへ戻す
		void RestorePreviewMaterial();

		//--------- accessor -----------------------------------------------------

		UUID GetTargetEntityUUID() const { return previewEntityUUID_; }
		// 元のMaterialを復元してから対象を変更する
		void SetTargetEntityUUID(UUID entityUUID);
		bool IsMaterialApplied() const { return previewMaterialApplied_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 選択中と適用済みの対象
		UUID previewEntityUUID_{};
		UUID appliedPreviewEntityUUID_{};
		ShaderGraphTarget appliedPreviewTarget_ = ShaderGraphTarget::Mesh;
		// 適用前のMaterialとWorldの生存確認
		AssetID previewOriginalMaterial_{};
		ECSWorld* appliedWorld_ = nullptr;
		std::weak_ptr<const ECSWorldLifetime> appliedWorldLifetime_{};
		// 復元が必要か
		bool previewMaterialApplied_ = false;
	};
}
