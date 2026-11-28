#pragma once

//============================================================================
//	include
//============================================================================
#include "LightGPUTypes.h"
#include <Engine/Core/Rendering/Renderer/Backends/Common/DefaultStructuredInstanceBuffer.h>

namespace Engine {

	class GraphicsCore;
	class RenderBufferRegistry;
	struct PerViewLightSet;

	//========================================================================
	//	WorldLightIndex class
	//	GIと反射のヒット位置に届くライトを探す
	//========================================================================
	class WorldLightIndex {
	public:
		uint32_t Publish(GraphicsCore& graphics, const PerViewLightSet& lights, RenderBufferRegistry& registry);
		void Release();

	private:
		struct Bounds {
			Vector3 lower{}, upper{};
			bool operator==(const Bounds&) const = default;
		};
		struct Node {
			Vector3 lower{};
			uint32_t next = 0;
			Vector3 upper{};
			uint32_t light = UINT32_MAX;
		};
		static_assert(sizeof(Node) == 32);
		struct CellRange {
			std::array<uint32_t, 3> first{}, last{};
			std::array<int32_t, 3> center{};
			float radius = 0.0f;
			bool operator==(const CellRange&) const = default;
		};

		std::vector<Bounds> bounds_;
		std::vector<Vector4> spheres_;
		std::vector<uint32_t> order_;
		std::vector<Node> nodes_;
		std::vector<CellRange> ranges_, previousRanges_;
		std::vector<uint32_t> offsets_, indices_;
		Bounds gridBounds_{};
		bool gridActive_ = false;
		uint32_t maxCellLights_ = 0;
		uint32_t gridSide_ = 0;
		DefaultStructuredInstanceBuffer<Node> buffer_{ "gWorldLightNodes" };
		float buildCost_ = 0.0f;
		bool initialized_ = false;

		void Update(const PerViewLightSet& lights);
		bool UpdateGrid(uint32_t side);
		uint32_t Build(uint32_t first, uint32_t last);
		float Refit();
	};
}

