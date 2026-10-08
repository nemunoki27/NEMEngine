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
	// パイプラインごとのスロット解決結果を保持する
	//============================================================================
	class PipelineBindingCache {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		// スロットの識別子でAddSlotの戻り値
		using SlotID = uint16_t;
		// 無効なスロットID
		static constexpr SlotID kInvalidSlot = UINT16_MAX;

		PipelineBindingCache() = default;
		~PipelineBindingCache() = default;

		// 初期化時に名前検索のスロットを登録する
		SlotID AddSlot(std::string_view name, ShaderBindingKind kind);

		// 初期化時にレジスター検索のスロットを登録する
		SlotID AddSlotByRegister(ShaderBindingKind kind, UINT bindPoint, UINT space = 0);

		// 使用するパイプラインが変わったらスロットを解決する
		void Sync(const PipelineState& pipeline);

		//--------- accessor -----------------------------------------------------

		// 同期済みの参照を返しパイプラインの寿命中だけ有効
		const RootBindingLocation* Get(SlotID id) const;

		// スロットが有効なロケーションを持つか
		bool Has(SlotID id) const;

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct SlotEntry {

			// 空ならレジスターで検索
			std::string name;
			ShaderBindingKind kind;
			UINT bindPoint = 0;
			UINT space = 0;
			const RootBindingLocation* location = nullptr;
		};

		//--------- variables ----------------------------------------------------

		// 初期化時に登録した検索条件と借用結果
		std::vector<SlotEntry> slots_;
		// 再生成を区別する同期済みパイプラインのID
		uint64_t lastPipelineID_ = 0;
	};
} // Engine

