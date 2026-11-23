#pragma once

#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

namespace Engine {

	// Planeの固定点を追従させ、布の変形を更新する
	class ClothSystem : public ISystem {
	public:
		void Update(ECSWorld& world, SystemContext& context) override;
		const char* GetName() const override { return "ClothSystem"; }
	};
}
