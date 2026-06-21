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
	//	AddSlot / AddSlotByRegisterでスロットを事前登録し、毎フレームSyncを
	// 呼ぶことでパイプラインが変わった時だけFindBindingByName / FindBindingを実行する
	// スロットIDでO(1)アクセス可能
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

		// 名前ベースのスロットをInit時に登録し識別用IDを返す
		SlotID AddSlot(std::string_view name, ShaderBindingKind kind);

		// register/spaceベースのスロットをInit時に登録し識別用IDを返す
		SlotID AddSlotByRegister(ShaderBindingKind kind, UINT bindPoint, UINT space = 0);

		// パイプラインが変わった時だけ全スロットを再解決する毎フレーム呼び出し
		void Sync(const PipelineState& pipeline);

		//--------- accessor -----------------------------------------------------

		// スロットIDに対応するロケーションを取得するSync後に有効
		const RootBindingLocation* Get(SlotID id) const;

		// スロットが有効なロケーションを持つか
		bool Has(SlotID id) const;

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct SlotEntry {

			// 空の場合はregister/spaceで検索
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

