#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Queue/RenderQueue.h>
#include <Engine/Core/Graphics/Render/View/RenderViewTypes.h>

// c++
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	RenderPassItemCollector structures
	//============================================================================

	// 描画アイテムリスト
	struct RenderPassItemList {

		std::vector<const RenderItem*> items;

		// アイテム削除
		void Clear() { items.clear(); }
		// アイテムが空か
		bool IsEmpty() const { return items.empty(); }
	};
	// 1回のシーン実行中だけ使う、描画フェーズごとの一時バケット
	struct RenderPassPhaseBuckets {

		std::unordered_map<std::string, RenderPassItemList> phaseToItems{};

		// データクリア
		void Clear() { phaseToItems.clear(); }
		// 描画フェーズに対してアイテムリストを返す
		const RenderPassItemList* Find(const std::string& renderPhase) const;
		const RenderPassItemList* Find(const std::string_view& renderPhase) const;
	};

	//============================================================================
	//	RenderPassItemCollector class
	//	描画パス実行前のビュー別収集、振り分けを行うクラス
	//============================================================================
	class RenderPassItemCollector {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderPassItemCollector() = default;
		~RenderPassItemCollector() = default;

		// ビューの中から描画フェーズに対して有効なアイテムを収集し、描画アイテムリストに振り分ける
		static void CollectForView(const RenderSceneBatch& batch, const std::string& renderPhase,
			const ResolvedRenderView& view, RenderPassItemList& outList);
		// 1回のシーン実行で使い回す、フェーズごとのバケットを構築する
		static void BuildBucketsForViewAndScene(const RenderSceneBatch& batch, const ResolvedRenderView& view,
			UUID sceneInstanceID, RenderPassPhaseBuckets& outBuckets);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// 描画アイテムが描画ビューに対して可視か
		static bool IsVisibleToView(const RenderItem& item, const std::string& renderPhase, const ResolvedRenderView& view);
		// 描画フェーズ比較を含まない、ビュー可視判定本体
		static bool IsVisibleToView(const RenderItem& item, const ResolvedRenderView& view);
	};
} // Engine
