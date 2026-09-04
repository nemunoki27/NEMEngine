#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfile.h>
#include <Engine/Editor/Tools/Core/IEditorTool.h>

// c++
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Engine {

	//============================================================================
	//	RenderFeatureProfileTool class
	//	ComputeとDispatchRaysを同じProfile上で編集するツール
	//============================================================================
	class RenderFeatureProfileTool :
		public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderFeatureProfileTool() = default;
		~RenderFeatureProfileTool() override = default;

		void Tick(ToolContext& context) override;
		void OpenEditorTool() override;
		void DrawEditorTool(const EditorToolContext& context) override;
		void OpenAsset(AssetID assetID);

		const ToolDescriptor& GetDescriptor() const override {

			return descriptor_;
		}

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		ToolDescriptor descriptor_{
			.id = "engine.render_features",
			.name = "レンダー機能設定",
			.category = "レンダリング",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::AllowPlayMode,
			.order = 0,
		};

		bool openWindow_ = false;
		AssetID requestedProfile_{};
		AssetID observedProfile_{};
		UUID selectedPass_{};
		UUID selectedGroup_{};
		std::vector<UUID> selectedPasses_{};
		std::string statusMessage_{};
		bool statusError_ = false;

		void DrawWindow(const EditorToolContext& context);
		void DrawColorPipeline();
		void DrawPassList(const EditorToolContext& context);
		void DrawPassDetail(const EditorToolContext& context);
		bool DrawSelectedPassControls(RenderFeatureProfileAsset& profile);
		bool DrawSelectedPassApplicationSettings(
			RenderFeatureProfileAsset& profile,
			RenderFeaturePassSettings& pass);
		void DrawSelectedGroupDetail(RenderFeatureProfileAsset& profile);
		void DrawOutputs(RenderFeaturePassSettings& pass);
		void DrawResources(const EditorToolContext& context,
			RenderFeaturePassSettings& pass);
		// パスへ設定できるアセットか判定する
		static bool IsPassMaterialSource(
			AssetType assetType, std::string_view assetPath);
		// MaterialまたはCompute Shaderからパス用Materialを解決する
		AssetID ResolvePassMaterial(const EditorToolContext& context,
			AssetID assetID, AssetType assetType, std::string_view assetPath);
		static std::string MakeReferenceLabel(
			const RenderFeatureProfileAsset& profile,
			const RenderFeatureOutputReference& reference,
			const char* emptyLabel);
		static bool DrawOutputReferenceCombo(const char* label,
			const RenderFeatureProfileAsset& profile,
			const RenderFeaturePassSettings& owner,
			RenderFeatureOutputReference& reference,
			const char* emptyLabel);
		static bool DrawSamplerSettings(
			PipelineStaticSamplerSettings& settings);
		bool ImportProfileSettings(const EditorToolContext& context,
			AssetID sourceProfile);
		bool EnsureProfile(const EditorToolContext& context);
		void ClearSelection();
		void SetDirty();
	};
} // Engine
