#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Runtime/PrefabGenerationContext.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabInstantiation.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>

namespace Engine {

	//============================================================================
	//	PrefabNestedInstanceBuilder class
	//	ネストPrefabの保存IDと参照を対応付けて生成する
	//============================================================================
	class PrefabNestedInstanceBuilder {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PrefabNestedInstanceBuilder(PrefabGenerationContext& context, const PrefabInstantiateDesc& desc,
			PrefabInstantiateResult& result, PrefabReferenceRemapper::LocalFileIDMap& localMap);
		// 保存されたネスト宣言と追加Instanceを復元する
		bool Build(const nlohmann::json& fileJson);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		PrefabGenerationContext& context_;
		const PrefabInstantiateDesc& desc_;
		PrefabInstantiateResult& result_;
		PrefabReferenceRemapper::LocalFileIDMap& localMap_;

		//--------- functions ----------------------------------------------------

		// 同じSlotに保存された差分を探す
		const PrefabInstanceData* FindRestored(UUID nestedSlotID) const;
		// 複製したネストのIDと内部参照を更新する
		void FreshenData(PrefabInstanceData& data, UUID ownerInstanceID,
			const PrefabReferenceRemapper::LocalFileIDMap& externalMap, bool preserveLocalFileIDs);
		// 宣言元と保存済みInstanceのIDを対応付ける
		void AppendRestoredMap(const PrefabInstanceData& declaration, const PrefabInstanceData& restored);
		// 差分を適用して親の生成結果へ加える
		bool Instantiate(PrefabInstanceData data, bool preserveSceneIDs, bool isPrefabAssetNested);
	};
}
