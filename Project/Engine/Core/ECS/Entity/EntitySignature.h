#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Config/ECSConfig.h>

// c++
#include <array>

namespace Engine {

	//============================================================================
	//	EntitySignature struct
	//	エンティティのArchetypeを表すシグネチャ
	//============================================================================
	struct EntitySignature {

		// 64ビットの単位でコンポーネントの有無を管理するビットセット
		static constexpr uint32_t kWordCount = (kMaxComponentTypes + 63) / 64;
		std::array<uint64_t, kWordCount> words{};

		// 比較演算子
		friend bool operator==(const EntitySignature& signatureA, const EntitySignature& signatureB);

		// 指定のtypeIDのビットをセット、リセット、テストする関数
		void Set(uint32_t typeID);
		void Reset(uint32_t typeID);
		bool Test(uint32_t typeID) const;

		// rhsのシグネチャがこのシグネチャに含まれているか
		bool Contains(const EntitySignature& rhs) const;
	};

	// シグネチャのハッシュ関数
	struct EntitySignatureHash {

		// シグネチャのビットセットをまとめてハッシュ値に変換する関数
		size_t operator()(const EntitySignature& signature) const noexcept;
	};
} // Engine