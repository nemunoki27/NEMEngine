#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>

namespace Engine {

	//============================================================================
	//	ShaderGraphScenePreview class
	//	シーンのMaterial差替えと復元状態を所有する
	//============================================================================
	class ShaderGraphScenePreview {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 対象のMaterialをプレビューへ差し替える
		bool ApplyPreviewMaterial(const EditorToolContext& context, ShaderGraphTarget target,
			AssetID material, std::string& statusMessage);
		// 差替え前のMaterialへ戻す
		void RestorePreviewMaterial(const EditorToolContext& context);

		//--------- accessor -----------------------------------------------------

		UUID& GetTargetEntityUUID() { return previewEntityUUID_; }
		bool IsMaterialApplied() const { return previewMaterialApplied_; }
		double& GetCompileDeadline() { return previewCompileDeadline_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		UUID previewEntityUUID_{};
		UUID appliedPreviewEntityUUID_{};
		ShaderGraphTarget appliedPreviewTarget_ =
			ShaderGraphTarget::Mesh;
		AssetID previewOriginalMaterial_{};
		bool previewMaterialApplied_ = false;
		double previewCompileDeadline_ = 0.0;
	};
}
