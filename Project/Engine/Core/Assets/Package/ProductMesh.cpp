#include "ProductMesh.h"

//============================================================================
//	include
//============================================================================
#include "ProductContentPack.h"
#include <Engine/Core/Rendering/Meshes/Import/MeshFileImporter.h>
#include <algorithm>
#include <array>

namespace {
	using namespace Engine;
	using namespace ProductBinary;

	struct MeshHeader {
		uint32_t magic = 0x4D4D454E; // NEMM
		uint32_t version = 1;
		std::array<uint64_t, 3> sizes{};
	};

	template<class A>
	void Node(A& a, MeshNode& node, unsigned depth) {
		if (depth > 512) throw std::runtime_error("メッシュのノード階層が深すぎます");
		String(a, node.name);
		Scalars(a, node.transform, node.localMatrix.m);
		Sequence(a, node.children, [&](MeshNode& child) { Node(a, child, depth + 1); });
	}

	template<class A>
	void Geometry(A& a, ImportedMeshAsset& mesh) {
		Array(a, mesh.vertices);
		Array(a, mesh.vertexColors);
		Array(a, mesh.indices);
		Sequence(a, mesh.subMeshes, [&](SubMeshDesc& sub) {
			Scalars(a, sub.indexOffset, sub.indexCount, sub.meshletOffset, sub.meshletCount);
			String(a, sub.name);
			String(a, sub.materialName);
			ProductMesh::TexturePaths(a, sub.defaultTextures);
			Scalars(a, sub.hasBaseColorTexture, sub.baseColor, sub.surfaceMode, sub.alphaCutoff, sub.boundsCenter, sub.boundsRadius);
		});
		Array(a, mesh.vertexSubMeshIndices);
		Array(a, mesh.meshlets);
		Array(a, mesh.meshletVertexIndices);
		Array(a, mesh.meshletPrimitiveIndices);
		Node(a, mesh.rootNode, 0);
		Scalars(a, mesh.isSkinned, mesh.boneCount);
		Array(a, mesh.vertexInfluences);
	}

	template<class A, class V, class F>
	void NamedEntries(A& a, std::unordered_map<std::string, V>& values, F&& serialize) {
		const auto count = a.Count(values.size());
		if constexpr (std::is_same_v<A, Reader>) {
			for (uint32_t i = 0; i < count; ++i) {
				std::string name;
				String(a, name);
				auto [it, inserted] = values.try_emplace(std::move(name));
				if (!inserted) throw std::runtime_error("メッシュ内の名前が重複しています");
				serialize(it->second);
			}
		} else {
			std::vector<std::string> names;
			for (const auto& [name, value] : values) names.push_back(name);
			std::sort(names.begin(), names.end());
			for (auto& name : names) {
				String(a, name);
				serialize(values.at(name));
			}
		}
	}

	template<class A>
	void Animation(A& a, SkinnedMeshAnimationSet& animation) {
		auto& skeleton = animation.skeleton;
		Scalars(a, skeleton.root, animation.valid);
		String(a, skeleton.name);
		NamedEntries(a, skeleton.jointMap, [&](int32_t& i) { Scalars(a, i); });
		NamedEntries(a, skeleton.jointPathMap, [&](int32_t& i) { Scalars(a, i); });
		Sequence(a, skeleton.joints, [&](Joint& joint) {
			Scalars(a, joint.transform, joint.isParentTransform, joint.isSkinBone, joint.localMatrix.m, joint.skeletonSpaceMatrix.m);
			String(a, joint.name);
			String(a, joint.nodePath);
			Array(a, joint.children);
			int32_t parent = joint.parent.value_or(-1);
			Scalars(a, joint.index, parent);
			if constexpr (std::is_same_v<A, Reader>) {
				if (parent >= 0) joint.parent = parent;
			}
		});
		Sequence(a, animation.skinCluster.inverseBindPoseMatrices, [&](Matrix4x4& matrix) { Scalars(a, matrix.m); });
		Sequence(a, animation.clipOrder, [&](std::string& name) { String(a, name); });
		NamedEntries(a, animation.clips, [&](AnimationData& clip) {
			Scalars(a, clip.duration);
			NamedEntries(a, clip.nodeAnimations, [&](NodeAnimation& channel) {
				Array(a, channel.scale.keyframes);
				Array(a, channel.rotate.keyframes);
				Array(a, channel.translate.keyframes);
			});
		});
	}
}

std::span<const uint8_t> Engine::ProductMesh::ReadSection(const std::filesystem::path& path,
	Section section, std::vector<uint8_t>& storage) {

	if (!ProductContentPack::Read(path, storage)) throw std::runtime_error("製品メッシュを読み込めません");
	Reader reader(storage);
	MeshHeader header;
	Scalars(reader, header);
	if (header.magic != MeshHeader{}.magic || header.version != 1) throw std::runtime_error("製品メッシュの形式が不正です");
	auto remaining = std::span<const uint8_t>(storage).subspan(sizeof(header));
	std::span<const uint8_t> result;
	for (size_t i = 0; i < header.sizes.size(); ++i) {
		if (header.sizes[i] > remaining.size()) throw std::runtime_error("製品メッシュの範囲が不正です");
		if (i == static_cast<size_t>(section)) result = remaining.first(header.sizes[i]);
		remaining = remaining.subspan(header.sizes[i]);
	}
	if (!remaining.empty()) throw std::runtime_error("製品メッシュの長さが不正です");
	return result;
}

bool Engine::ProductMesh::Cook(const std::filesystem::path& source, AssetID id, std::vector<uint8_t>& bytes) {

	auto mesh = MeshFileImporter::ImportFile(id, source);
	if (mesh.vertices.empty() || mesh.indices.empty()) return false;
	MeshHeader header;
	bytes.resize(sizeof(header));
	Writer writer(bytes);
	Geometry(writer, mesh);
	header.sizes[0] = bytes.size() - sizeof(header);
	auto start = bytes.size();
	CookLayout(source, bytes);
	header.sizes[1] = bytes.size() - start;
	start = bytes.size();
	if (mesh.isSkinned) {
		auto animation = SkinnedMeshAnimationManager::ImportAnimationFile(id, source);
		Animation(writer, animation);
	}
	header.sizes[2] = bytes.size() - start;
	std::memcpy(bytes.data(), &header, sizeof(header));
	return true;
}

void Engine::ProductMesh::LoadGeometry(const std::filesystem::path& path, ImportedMeshAsset& mesh) {
	std::vector<uint8_t> storage;
	Reader reader(ReadSection(path, Section::Geometry, storage));
	Geometry(reader, mesh);
	if (reader.Remaining()) throw std::runtime_error("製品メッシュの頂点領域が不正です");
}

void Engine::ProductMesh::LoadAnimation(const std::filesystem::path& path, SkinnedMeshAnimationSet& animation) {
	std::vector<uint8_t> storage;
	Reader reader(ReadSection(path, Section::Animation, storage));
	if (reader.Remaining()) Animation(reader, animation);
	if (reader.Remaining()) throw std::runtime_error("製品メッシュのアニメーション領域が不正です");
}
