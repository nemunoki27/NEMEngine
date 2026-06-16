#include "SkeletonBuilder.h"

//============================================================================
//	include
//============================================================================
#include <optional>

namespace {

	// MeshNodeを再帰的にたどってJointを積む
	int32_t CreateJointRecursive(const Engine::MeshNode& node,
		const std::optional<int32_t> parent, std::vector<Engine::Joint>& joints) {

		Engine::Joint joint{};
		joint.name = node.name;
		joint.localMatrix = node.localMatrix;
		joint.transform = node.transform;
		joint.index = static_cast<int32_t>(joints.size());
		joint.parent = parent;
		joints.emplace_back(joint);
		for (const auto& child : node.children) {

			int32_t childIndex = CreateJointRecursive(child, joint.index, joints);
			joints[joint.index].children.emplace_back(childIndex);
		}
		return joint.index;
	}
}

//============================================================================
//	SkeletonBuilder functions
//============================================================================
Engine::Skeleton Engine::BuildSkeletonFromMeshNode(const MeshNode& rootNode) {

	Skeleton skeleton{};
	skeleton.root = CreateJointRecursive(rootNode, std::nullopt, skeleton.joints);
	for (const auto& joint : skeleton.joints) {

		skeleton.jointMap.emplace(joint.name, joint.index);
	}
	return skeleton;
}
