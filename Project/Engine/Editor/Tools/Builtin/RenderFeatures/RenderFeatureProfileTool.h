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
	class RenderFeatureProfileTool : public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderFeatureProfileTool() = default;
		~RenderFeatureProfileTool() override = default;

		// Assetの更新を編集セッションへ反映する
		void Tick(ToolContext& context) override;
		// 編集画面を開く
		void OpenEditorTool() override;
		// 編集画面と未保存確認を描画する
		void DrawEditorTool(const EditorToolContext& context) override;
		// 指定Assetへの切替を要求する
		void OpenAsset(AssetID assetID);
		// 終了前に未保存編集を確認する
		bool HasPendingEdits() const override;
		// 終了時の保存確認を要求する
		void RequestResolvePendingEdits() override;
		// 未保存確認の結果を受け取る
		EditorToolCloseResult ConsumePendingEditCloseResult() override;

		//--------- accessor -----------------------------------------------------

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 階層内のドラッグ対象
		struct DragDropPayload {

			RenderFeatureHierarchyItemType type = RenderFeatureHierarchyItemType::Pass; // 対象の種別
			uint64_t id = 0;															// 対象の固定ID
		};

		// 階層走査後に適用する操作
		enum class PendingActionType : uint8_t {

			None,
			GroupSelection,
			UngroupPass,
			DeleteItem,
			MoveToGroup,
		};

		// 階層走査中に保持する操作要求
		struct PendingAction {

			PendingActionType type = PendingActionType::None;								// 要求する操作
			RenderFeatureHierarchyItemType itemType = RenderFeatureHierarchyItemType::Pass; // 対象の種別
			UUID item{};																	// 操作対象の固定ID
			UUID targetGroup{};																// 移動先のグループ
		};

		//--------- variables ----------------------------------------------------

		// ツール一覧への登録情報
		ToolDescriptor descriptor_{
			.id = "engine.render_features",
			.name = "Render Passes",
			.category = "レンダリング",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::AllowPlayMode,
			.order = 0,
		};

		bool openWindow_ = false;										  // 編集画面の表示状態
		bool pendingClose_ = false;										  // 未保存確認後の終了要求
		AssetID pendingAsset_{};										  // 未保存確認後の切替先
		RenderFeatureEditSession editSession_;							  // 編集値と保存状態
		UUID selectedPass_{};											  // 詳細を表示するPass
		UUID selectedGroup_{};											  // 詳細を表示するGroup
		std::vector<UUID> selectedPasses_{};							  // 一括操作するPass
		bool resolvePendingEdits_ = false;								  // Editor終了時の確認要求
		EditorToolCloseResult closeResult_ = EditorToolCloseResult::None; // 未保存確認の結果

		//--------- functions ----------------------------------------------------

		// ツールの編集画面を表示する
		void DrawWindow(const EditorToolContext& context);
		// 未保存編集の確認を表示する
		void DrawUnsavedChangesPopup(const EditorToolContext& context);
		// 別アセットへの切替を要求する
		void RequestAssetSwitch(AssetID assetID);
		// PassとGroupの一覧を表示する
		void DrawPassList(const EditorToolContext& context);
		// 選択Passの設定を編集する
		void DrawPassDetail(const EditorToolContext& context);
		// 選択Passの並べ替えと削除を行う
		bool DrawSelectedPassControls(RenderFeatureProfileAsset& profile);
		// 選択Passの適用対象を編集する
		bool DrawSelectedPassApplicationSettings(
			const EditorToolContext& context, RenderFeatureProfileAsset& profile, RenderFeaturePassSettings& pass);
		// 選択Groupの設定を編集する
		void DrawSelectedGroupDetail(const EditorToolContext& context, RenderFeatureProfileAsset& profile);
		// Passの出力設定を編集する
		void DrawOutputs(RenderFeaturePassSettings& pass);
		// Passの入力資源を編集する
		void DrawResources(const EditorToolContext& context, RenderFeaturePassSettings& pass);
		// 参照先の出力名を表示用に組み立てる
		static std::string MakeReferenceLabel(
			const RenderFeatureProfileAsset& profile, const RenderFeatureOutputReference& reference, const char* emptyLabel);
		// 参照するPass出力を選択する
		static bool DrawOutputReferenceCombo(const char* label, const RenderFeatureProfileAsset& profile,
			const RenderFeaturePassSettings& owner, RenderFeatureOutputReference& reference, const char* emptyLabel);
		// Sampler設定を編集する
		static bool DrawSamplerSettings(PipelineStaticSamplerSettings& settings);
		// 別Profileの設定を取り込む
		bool ImportProfileSettings(const EditorToolContext& context, AssetID sourceProfile);
		// PassとGroupの選択を解除する
		void ClearSelection();
	};
}
