#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfile.h>

// c++
#include <string>
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

		const RenderFeaturePassSettings* pass = nullptr;
		const RenderFeatureHierarchyItem* selectionGroup = nullptr;
		RenderFeatureOutputReference source{};
		bool selectionBegin = false;
		bool selectionEnd = false;
	};

	// Anchor単位で依存順に並べた実行計画
	struct RenderFeatureExecutionPlan {

		std::vector<RenderFeaturePlanNode> nodes{};
		RenderFeatureOutputReference sceneColorOutput{};
		std::string diagnostic{};

		bool IsValid() const { return diagnostic.empty(); }
	};

	// Profileを検証して実行順へ変換する
	class RenderFeatureProfileRuntime {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderFeatureProfileRuntime() = default;
		~RenderFeatureProfileRuntime() = default;

		void Rebuild(const RenderFeatureProfileAsset& profile);
		RenderFeatureExecutionPlan BuildPlan(
			RenderFeatureAnchor anchor, RenderViewKind viewKind) const;
		bool IsItemIsolated(const RenderItem& item) const;
		bool IsGroupEnabled(
			const RenderFeatureHierarchyItem& group) const;
		bool IsPassHierarchyEnabled(UUID passID) const;

		//--------- accessor -----------------------------------------------------

		const RenderFeatureProfileAsset& GetProfile() const { return profile_; }
		const std::string& GetDiagnostic() const { return diagnostic_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		bool IsSelectionEnabled(
			const RenderFeatureHierarchyItem& item) const;

		//--------- variables ----------------------------------------------------

		RenderFeatureProfileAsset profile_{};
		std::string diagnostic_{};
		// Passから選択適用項目への参照を再構築時に解決する
		std::unordered_map<uint64_t,
			const RenderFeatureHierarchyItem*> selectionGroupsByPass_{};
		// 通常描画から除外する分離適用項目
		std::vector<const RenderFeatureHierarchyItem*> isolatedGroups_{};
		// Group自身を含む祖先Group列を有効判定へ使う
		std::unordered_map<const RenderFeatureHierarchyItem*,
			std::vector<const RenderFeatureHierarchyItem*>> groupLineages_{};
		// Passを所有するGroup列を実行時の有効判定へ使う
		std::unordered_map<uint64_t,
			std::vector<const RenderFeatureHierarchyItem*>> passLineages_{};

		// Profile全体のIDと入出力参照を検証する
		bool ValidateProfile();
	};

	// RenderItemを選択適用の3条件で判定する
	bool MatchesRenderFeatureSelection(const RenderItem& item,
		const RenderFeatureSelectionSettings& selection);
} // Engine
