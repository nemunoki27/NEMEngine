#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Vector4.h>

// c++
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

namespace Engine {

	struct ImportedMeshAsset;
	struct WellForGPU;

	// GIの受光面とプローブの距離判定に使うローカル空間の三角形階層
	class MeshSurfaceGeometry {
	public:
		struct PoseBounds {
			Vector4 sphere{0, 0, 0, -1};
			Vector3 halfExtent{-1, -1, -1};
		};
		static std::shared_ptr<const MeshSurfaceGeometry> Build(const ImportedMeshAsset& mesh);
		bool IntersectsSphere(uint32_t subMesh, const Vector3& center, float radius) const;
		PoseBounds GetSkinnedBounds(uint32_t subMesh, std::span<const WellForGPU> palette) const;

	private:
		struct Triangle {
			std::array<uint32_t, 3> indices;
		};
		struct Node {
			Vector3 minimum, maximum;
			uint32_t first = 0;
			uint32_t count = 0;
		};
		struct JointBounds {
			Vector3 minimum, maximum;
			uint32_t joint = UINT32_MAX;
		};
		std::vector<JointBounds> jointBounds_;
		std::vector<Vector3> positions_;
		std::vector<std::pair<uint32_t,uint32_t>> ranges_;
		mutable std::once_flag hierarchyReady_;
		mutable std::vector<Triangle> triangles_;
		mutable std::vector<Node> nodes_;
		mutable std::vector<uint32_t> roots_;

		bool BuildJointBounds(const ImportedMeshAsset& mesh);
		void BuildHierarchy() const;
		void BuildNode(uint32_t node, uint32_t first, uint32_t count) const;
	};
}
