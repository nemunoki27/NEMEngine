#include "PrimitiveBatchResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>

// c++
#include <span>

//============================================================================
//	PrimitiveBatchResources classMethods
//============================================================================
void Engine::PrimitiveBatchResources::Init(GraphicsCore& graphicsCore) {

	if (initialized_) {
		return;
	}
	instances_.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	initialized_ = true;
}

void Engine::PrimitiveBatchResources::UploadInstances(const std::vector<PrimitiveInstanceData>& instances) {

	instanceCount_ = static_cast<uint32_t>(instances.size());
	if (instanceCount_ == 0) {
		return;
	}
	instances_.Upload(std::span<const PrimitiveInstanceData>(instances.data(), instances.size()));
}

void Engine::ApplyPrimitiveShapeToInstance(const PrimitiveRendererComponent& renderer, PrimitiveInstanceData& instance) {

	if (renderer.type == PrimitiveType::Ring) {

		const PrimitiveRingParams& ring = renderer.ring;
		instance.shapeParams0 = Vector4(
			ring.outerRadius, ring.innerRadius, ring.startAngle, ring.endAngle - ring.startAngle);
		instance.shapeParams1.z = 1.0f;
	}
	if (renderer.type == PrimitiveType::Cylinder) {

		const PrimitiveCylinderParams& cylinder = renderer.cylinder;
		instance.shapeParams0 = Vector4(
			cylinder.topRadius, cylinder.centerRadius, cylinder.bottomRadius, cylinder.height);
		instance.shapeParams1 = Vector4(
			cylinder.topRadiusWeight, cylinder.bottomRadiusWeight, 0.0f, 1.0f);
		instance.topColor = cylinder.topColor;
		instance.centerColor = cylinder.centerColor;
		instance.bottomColor = cylinder.bottomColor;
	}
}
