#pragma once

#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

namespace Engine {

	class ParticleFieldAssetCache;
	struct ClothGPUVectorAtlas;
	class GraphicsCore;
	class PipelineStateCache;

	// Planeの固定点を追従させ、布の変形を更新する
	class ClothSystem : public ISystem {
	public:
		ClothSystem();
		~ClothSystem() override;
		void Update(ECSWorld& world, SystemContext& context) override;
		static void DispatchGPU(ECSWorld& world, GraphicsCore& graphics, RenderAssetLibrary& assets, PipelineStateCache& pipelines);
		const char* GetName() const override { return "ClothSystem"; }
	private:
		std::unique_ptr<ParticleFieldAssetCache> fieldAssets_;
		std::shared_ptr<ClothGPUVectorAtlas> windAtlas_;
	};
}
