#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Core/ECS/System/Context/SystemContext.h>

namespace Engine {

	//============================================================================
	//	MonoBehavior class
	//	コンポーネントとして使用されるクラス
	//============================================================================
	class MonoBehavior {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		MonoBehavior() = default;
		virtual ~MonoBehavior() = default;

		//========================================================================
		//	初期化メソッド
		//========================================================================

		virtual void Awake(ECSWorld&, const SystemContext&, const Entity&) {}
		virtual void Start(ECSWorld&, const SystemContext&, const Entity&) {}

		//========================================================================
		//	エンティティの状態変化に伴うメソッド
		//========================================================================

		virtual void OnEnable(ECSWorld&, const SystemContext&, const Entity&) {}
		virtual void OnDisable(ECSWorld&, const SystemContext&, const Entity&) {}
		virtual void OnDestroy(ECSWorld&, const SystemContext&, const Entity&) {}

		//========================================================================
		//	更新メソッド
		//========================================================================

		virtual void FixedUpdate(ECSWorld&, const SystemContext&, const Entity&) {}
		virtual void Update(ECSWorld&, const SystemContext&, const Entity&) {}
		virtual void LateUpdate(ECSWorld&, const SystemContext&, const Entity&) {}
	};
} // Engine