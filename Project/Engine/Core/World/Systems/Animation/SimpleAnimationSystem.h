#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Animation/Evaluation/AnimationPropertySnapshot.h>
#include <Engine/Core/World/Components/Animation/SimpleAnimationComponent.h>

// c++
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	SimpleAnimationSystem class
	//	共通評価器の結果を選択したプロパティへ適用する
	//============================================================================
	class SimpleAnimationSystem : public ISystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		void Update(ECSWorld& world, SystemContext& context) override;
		void OnWorldExit(ECSWorld& world, SystemContext& context) override;
		static void RestoreEditPreviews(ECSWorld& world);
		const char* GetName() const override { return "SimpleAnimationSystem"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct BindingState {
			Entity target = Entity::Null();
			AnimationPropertyBinding binding{};
			std::optional<AnimationPropertyDescriptor> descriptor;
			std::vector<AnimationPreviewBaseValue> base;
			bool edit = false;
			bool visited = false;
		};
		struct Entry {
			Entity owner;
			Entity target;
			SimpleAnimationComponent* component;
			bool conflict = false;
		};
		struct BindingStorage {
			std::unordered_map<UUID, BindingState> values;
		};
		//--------- variables ----------------------------------------------------

		std::vector<Entry> entries_;

		//--------- functions ----------------------------------------------------

		// 捕捉した値を復元する
		static void Restore(ECSWorld& world, BindingState& state);
		// 単一値の再生と自動適用を進める
		void UpdateEntry(ECSWorld& world, SystemContext& context, Entry& entry);
	};
}
