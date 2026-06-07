#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>

// c++
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	PipelineBindingCache class
	// パイプラインのスロット解決結果をキャッシュするクラス
	//	AddSlot / AddSlotByRegister でスロットを事前登録し、毎フレーム Sync を
	// 呼ぶことでパイプラインが変わった時だけ FindBindingByName / FindBinding を実行する
	// スロットIDでO(1)アクセス可能
	//============================================================================
	class PipelineBindingCache {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		// スロットの識別子（AddSlotの戻り値）
		using SlotID = uint16_t;
		// 無効なスロットID
		static constexpr SlotID kInvalidSlot = UINT16_MAX;

		PipelineBindingCache() = default;
		~PipelineBindingCache() = default;

		// 名前ベースのスロットを登録し、識別用IDを返す（Init時に呼ぶ）
		SlotID AddSlot(std::string_view name, ShaderBindingKind kind);

		// register/spaceベースのスロットを登録し、識別用IDを返す（Init時に呼ぶ）
		SlotID AddSlotByRegister(ShaderBindingKind kind, UINT bindPoint, UINT space = 0);

		// パイプラインが変わった時だけ全スロットを再解決する（毎フレーム呼ぶ）
		void Sync(const PipelineState& pipeline);

		//--------- accessor -----------------------------------------------------

		// スロットIDに対応するロケーションを取得する（Sync後に有効）
		const RootBindingLocation* Get(SlotID id) const;

		// スロットが有効なロケーションを持つか
		bool Has(SlotID id) const;

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct SlotEntry {

			// 空の場合は register/space で検索
			std::string name;
			ShaderBindingKind kind;
			UINT bindPoint = 0;
			UINT space = 0;
			const RootBindingLocation* location = nullptr;
		};

		//--------- variables ----------------------------------------------------

		std::vector<SlotEntry> slots_;
		const PipelineState* lastPipeline_ = nullptr;
	};
} // Engine

