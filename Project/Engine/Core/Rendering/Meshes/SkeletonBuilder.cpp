#include "SkeletonBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/Import/MeshImportUtility.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// assimp
#include <assimp/scene.h>
#include <assimp/mesh.h>

// c++
#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {

	using NodeSet = std::unordered_set<const aiNode*>;
	using NodeNameMap = std::unordered_map<std::string, std::vector<const aiNode*>>;

	// ノード階層内の同名候補を収集する
	void CollectNodesByName(const aiNode* node, const aiString& name,
		std::vector<const aiNode*>& outNodes) {

		if (node->mName == name && node->mNumMeshes == 0) {
			outNodes.emplace_back(node);
		}
		for (uint32_t i = 0; i < node->mNumChildren; ++i) {
			CollectNodesByName(node->mChildren[i], name, outNodes);
		}
	}

	// ノード名ごとの実体を収集する
	void CollectNodeNames(const aiNode* node, NodeNameMap& outNames) {

		const std::string name = node->mName.C_Str();
		if (!name.empty()) {
			outNames[name].emplace_back(node);
		}
		for (uint32_t i = 0; i < node->mNumChildren; ++i) {
			CollectNodeNames(node->mChildren[i], outNames);
		}
	}

	// ルートからの子インデックス列を内部ノードパスにする
	std::string BuildNodePath(const aiNode* node) {

		if (!node) {
			return {};
		}

		std::vector<uint32_t> childIndices;
		const aiNode* current = node;
		while (current->mParent) {

			const aiNode* parent = current->mParent;
			auto childIt = std::find(parent->mChildren,
				parent->mChildren + parent->mNumChildren, current);
			if (childIt == parent->mChildren + parent->mNumChildren) {
				return {};
			}
			childIndices.emplace_back(static_cast<uint32_t>(childIt - parent->mChildren));
			current = parent;
		}

		std::string path = "0";
		for (auto it = childIndices.rbegin(); it != childIndices.rend(); ++it) {
			path += "/" + std::to_string(*it);
		}
		return path;
	}

	// Assimpが実体を設定できなかったボーンを名前から補完する
	const aiNode* FindBoneNodeFallback(const aiScene* scene, const aiBone* bone,
		std::string_view sourcePath) {

		std::vector<const aiNode*> candidates;
		CollectNodesByName(scene->mRootNode, bone->mName, candidates);
		if (candidates.empty()) {
			return nullptr;
		}
		if (1 < candidates.size()) {
			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
				"[SkeletonBuilder] Bone Nodeの代替候補を一意に決定できません source={} name={} count={}",
				sourcePath, bone->mName.C_Str(), candidates.size());
		}
		return candidates.front();
	}

	// スキンボーン実体とルートまでの親ノードを収集する
	void CollectSkinNodes(const aiScene* scene, NodeSet& skinBones,
		NodeSet& requiredNodes, std::string_view sourcePath) {

		for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {

			const aiMesh* mesh = scene->mMeshes[meshIndex];
			if (!mesh || !mesh->HasBones()) {
				continue;
			}
			for (uint32_t boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {

				const aiBone* bone = mesh->mBones[boneIndex];
				if (!bone) {
					continue;
				}

				const aiNode* boneNode = bone->mNode;
				if (!boneNode) {
					boneNode = FindBoneNodeFallback(scene, bone, sourcePath);
				}
				if (!boneNode) {
					Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
						"[SkeletonBuilder] Bone Nodeが見つかりません source={} name={}",
						sourcePath, bone->mName.C_Str());
					continue;
				}

				skinBones.insert(boneNode);
				for (const aiNode* node = boneNode; node; node = node->mParent) {
					requiredNodes.insert(node);
				}
			}
		}
	}

	// 対象ノードだけを元の階層順でジョイントに変換する
	int32_t CreateJointRecursive(const aiNode* node, const NodeSet& skinBones,
		const NodeSet& requiredNodes, const std::optional<int32_t> parent,
		const std::string& nodePath, std::vector<Engine::Joint>& joints) {

		if (!requiredNodes.contains(node)) {
			return -1;
		}

		const Engine::MeshNode meshNode = Engine::MeshImportUtility::ReadMeshNode(node);

		Engine::Joint joint{};
		joint.name = meshNode.name;
		joint.nodePath = nodePath;
		joint.localMatrix = meshNode.localMatrix;
		joint.transform = meshNode.transform;
		joint.isSkinBone = skinBones.contains(node);
		joint.index = static_cast<int32_t>(joints.size());
		joint.parent = parent;
		joints.emplace_back(std::move(joint));

		const int32_t jointIndex = static_cast<int32_t>(joints.size() - 1);
		for (uint32_t i = 0; i < node->mNumChildren; ++i) {

			const std::string childPath = nodePath + "/" + std::to_string(i);
			const int32_t childIndex = CreateJointRecursive(node->mChildren[i],
				skinBones, requiredNodes, jointIndex, childPath, joints);
			if (0 <= childIndex) {
				joints[jointIndex].children.emplace_back(childIndex);
			}
		}
		return jointIndex;
	}

	// 表示名は実ボーン優先、内部参照は一意なノードパスで登録する
	void BuildJointMaps(Engine::Skeleton& skeleton) {

		for (const Engine::Joint& joint : skeleton.joints) {

			skeleton.jointPathMap.emplace(joint.nodePath, joint.index);
			if (joint.name.empty()) {
				continue;
			}

			auto [nameIt, inserted] = skeleton.jointMap.emplace(joint.name, joint.index);
			if (!inserted &&
				!skeleton.joints[nameIt->second].isSkinBone && joint.isSkinBone) {
				nameIt->second = joint.index;
			}
		}
	}

	// 同名ノードを内部パス付きで警告する
	void LogDuplicateNodeNames(const aiScene* scene, const NodeSet& skinBones,
		std::string_view sourcePath) {

		NodeNameMap nodeNames;
		CollectNodeNames(scene->mRootNode, nodeNames);
		for (const auto& [name, nodes] : nodeNames) {

			if (nodes.size() < 2) {
				continue;
			}

			std::string paths;
			for (const aiNode* node : nodes) {
				if (!paths.empty()) {
					paths += ",";
				}
				paths += BuildNodePath(node);
				if (skinBones.contains(node)) {
					paths += "[skin]";
				}
			}
			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
				"[SkeletonBuilder] 同名Nodeを検出しました source={} name={} paths={}",
				sourcePath, name, paths);
		}
	}
}

//============================================================================
//	SkeletonBuilder functions
//============================================================================
Engine::Skeleton Engine::BuildSkinSkeleton(const aiScene* scene, std::string_view sourcePath) {

	Skeleton skeleton{};
	if (!scene || !scene->mRootNode) {
		return skeleton;
	}

	NodeSet skinBones;
	NodeSet requiredNodes;
	CollectSkinNodes(scene, skinBones, requiredNodes, sourcePath);
	if (skinBones.empty() || requiredNodes.empty()) {
		return skeleton;
	}

	LogDuplicateNodeNames(scene, skinBones, sourcePath);

	skeleton.root = CreateJointRecursive(scene->mRootNode, skinBones,
		requiredNodes, std::nullopt, "0", skeleton.joints);
	skeleton.name = scene->mRootNode->mName.C_Str();
	BuildJointMaps(skeleton);
	return skeleton;
}

int32_t Engine::FindSkeletonJointIndex(const Skeleton& skeleton,
	std::string_view jointReference) {

	if (jointReference.empty()) {
		return -1;
	}

	const std::string key(jointReference);
	auto pathIt = skeleton.jointPathMap.find(key);
	if (pathIt != skeleton.jointPathMap.end()) {
		return pathIt->second;
	}

	auto nameIt = skeleton.jointMap.find(key);
	return nameIt != skeleton.jointMap.end() ? nameIt->second : -1;
}

int32_t Engine::FindSkeletonJointIndex(const Skeleton& skeleton, const aiBone* bone) {

	if (!bone) {
		return -1;
	}
	if (bone->mNode) {

		const std::string nodePath = BuildNodePath(bone->mNode);
		auto pathIt = skeleton.jointPathMap.find(nodePath);
		if (pathIt != skeleton.jointPathMap.end()) {
			return pathIt->second;
		}
	}
	return FindSkeletonJointIndex(skeleton, bone->mName.C_Str());
}
