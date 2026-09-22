#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfile.h>
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include "RenderFeatureEditSession.h"

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

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }

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
		RenderFeatureEditSession editSession_;
		UUID selectedPass_{};
		UUID selectedGroup_{};
		std::vector<UUID> selectedPasses_{};

		// ツールの編集画面を表示する
		void DrawWindow(const EditorToolContext& context);
		// 色補正設定を編集する
		void DrawColorPipeline();
		// PassとGroupの一覧を表示する
		void DrawPassList(const EditorToolContext& context);
		// 選択Passの設定を編集する
		void DrawPassDetail(const EditorToolContext& context);
		// 選択Passの有効状態を編集する
		bool DrawSelectedPassControls(RenderFeatureProfileAsset& profile);
		// 選択Passの適用対象を編集する
		bool DrawSelectedPassApplicationSettings(
			const EditorToolContext& context, RenderFeatureProfileAsset& profile,
			RenderFeaturePassSettings& pass);
		// 選択Groupの設定を編集する
		void DrawSelectedGroupDetail(const EditorToolContext& context, RenderFeatureProfileAsset& profile);
		// Passの出力設定を編集する
		void DrawOutputs(RenderFeaturePassSettings& pass);
		// Passの入力資源を編集する
		void DrawResources(const EditorToolContext& context, RenderFeaturePassSettings& pass);
		// 参照先の出力名を表示用に組み立てる
		static std::string MakeReferenceLabel(
			const RenderFeatureProfileAsset& profile,
			const RenderFeatureOutputReference& reference,
			const char* emptyLabel);
		// 参照するPass出力を選択する
		static bool DrawOutputReferenceCombo(const char* label,
			const RenderFeatureProfileAsset& profile,
			const RenderFeaturePassSettings& owner,
			RenderFeatureOutputReference& reference,
			const char* emptyLabel);
		// Sampler設定を編集する
		static bool DrawSamplerSettings(PipelineStaticSamplerSettings& settings);
		// 別Profileの設定を取り込む
		bool ImportProfileSettings(const EditorToolContext& context, AssetID sourceProfile);
		// 必要なProfileの作成操作を表示する
		bool EnsureProfile(const EditorToolContext& context);
		// PassとGroupの選択を解除する
		void ClearSelection();
	};
} // Engine
