#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfile.h>

// c++
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Engine {

	struct RenderItem;
	enum class RenderViewKind;

	//============================================================================
	//	RenderFeatureProfileRuntime structures
	//============================================================================
	// 実行計画に含まれるPassと省略時の主入力
	struct RenderFeaturePlanNode {

		const RenderFeaturePassSettings* pass = nullptr;			// snapshot内のPass
		const RenderFeatureHierarchyItem* selectionGroup = nullptr; // 選択適用の対象
		RenderFeatureOutputReference source{};						// 主入力の参照
		bool selectionBegin = false;								// 選択描画の開始
		bool selectionEnd = false;									// 選択描画の終了
	};

	// Anchor単位で依存順に並べた実行計画
	struct RenderFeatureExecutionPlan {

		// Nodeが参照するProfileの寿命を保持する
		std::shared_ptr<const RenderFeatureProfileAsset> profileSnapshot;

		std::vector<RenderFeaturePlanNode> nodes{};		 // 依存順の実行項目
		RenderFeatureOutputReference sceneColorOutput{}; // SceneColorへ戻す出力
		std::string diagnostic{};						 // 計画の検証結果

		bool IsValid() const { return diagnostic.empty(); }
	};

	// Profileを検証して実行順へ変換する
	class RenderFeatureProfileRuntime {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 空の実行構成を初期化する
		RenderFeatureProfileRuntime() = default;
		// 所有するsnapshotと参照表を破棄する
		~RenderFeatureProfileRuntime() = default;
		// snapshotの寿命を共有する
		RenderFeatureProfileRuntime(const RenderFeatureProfileRuntime&) = default;
		RenderFeatureProfileRuntime& operator=(const RenderFeatureProfileRuntime&) = default;
		// 構築済みの参照表を移譲する
		RenderFeatureProfileRuntime(RenderFeatureProfileRuntime&&) noexcept = default;
		RenderFeatureProfileRuntime& operator=(RenderFeatureProfileRuntime&&) noexcept = default;

		// 検証後にsnapshotと参照表を公開する
		void Rebuild(const RenderFeatureProfileAsset& profile);
		// Viewと実行位置に合う計画を作成する
		RenderFeatureExecutionPlan BuildPlan(RenderFeatureAnchor anchor, RenderViewKind viewKind) const;
		// 通常描画から分離する対象か判定する
		bool IsItemIsolated(const RenderItem& item) const;
		// 指定Viewで通常描画から分離する対象か判定する
		bool IsItemIsolated(const RenderItem& item, RenderViewKind viewKind) const;
		// 選択適用に指定Viewで実行するPassがあるか判定する
		bool IsSelectionEnabled(const RenderFeatureHierarchyItem& item, RenderViewKind viewKind) const;
		// 祖先を含めたGroupの有効状態を判定する
		bool IsGroupEnabled(const RenderFeatureHierarchyItem& group) const;
		// Passの所属階層が有効か判定する
		bool IsPassHierarchyEnabled(UUID passID) const;

		//--------- accessor -----------------------------------------------------

		const RenderFeatureProfileAsset& GetProfile() const { return *profile_; }
		const std::string& GetDiagnostic() const { return diagnostic_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 計画と参照表が共有する構成
		std::shared_ptr<const RenderFeatureProfileAsset> profile_ = std::make_shared<const RenderFeatureProfileAsset>();
		std::string diagnostic_{}; // 構成の検証結果
		// Passから選択適用項目への参照を再構築時に解決する
		std::unordered_map<uint64_t, const RenderFeatureHierarchyItem*> selectionGroupsByPass_{};
		// 通常描画から除外する分離適用項目
		std::vector<const RenderFeatureHierarchyItem*> isolatedGroups_{};
		// Group自身を含む祖先Group列を有効判定へ使う
		std::unordered_map<const RenderFeatureHierarchyItem*, std::vector<const RenderFeatureHierarchyItem*>> groupLineages_{};
		// Passを所有するGroup列を実行時の有効判定へ使う
		std::unordered_map<uint64_t, std::vector<const RenderFeatureHierarchyItem*>> passLineages_{};

		//--------- functions ----------------------------------------------------

		// Viewと所属階層に合うPassの実行条件を判定する
		bool IsPassConfiguredActive(const RenderFeaturePassSettings& pass, RenderViewKind viewKind) const;
	};

	// RenderItemを選択適用の3条件で判定する
	bool MatchesRenderFeatureSelection(const RenderItem& item, const RenderFeatureSelectionSettings& selection);
}
