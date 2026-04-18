#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/System/Context/SystemContext.h>
#include <Engine/Core/ECS/World/ECSWorld.h>

namespace Engine {

	//============================================================================
	//	ISystem class
	//	ワールドを処理するシステムのインターフェース
	//============================================================================
	class ISystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ISystem() = default;
		virtual ~ISystem() = default;

		// ワールド切り替えで呼ばれる
		virtual void OnWorldEnter([[maybe_unused]] ECSWorld& world, [[maybe_unused]] SystemContext& context) {}
		virtual void OnWorldExit([[maybe_unused]] ECSWorld& world, [[maybe_unused]] SystemContext& context) {}

		// 各更新フェーズ
		virtual void FixedUpdate([[maybe_unused]] ECSWorld& world, [[maybe_unused]] SystemContext& context) {}
		virtual void Update([[maybe_unused]] ECSWorld& world, [[maybe_unused]] SystemContext& context) {}
		virtual void LateUpdate([[maybe_unused]] ECSWorld& world, [[maybe_unused]] SystemContext& context) {}

		//--------- accessor -----------------------------------------------------

		// デバッグ表示、検索用
		virtual const char* GetName() const = 0;
	};
} // Engine