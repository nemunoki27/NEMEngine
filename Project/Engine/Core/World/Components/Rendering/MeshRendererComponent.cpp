#include "MeshRendererComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <vector>

namespace {

	void ReadMeshRendererSettings(const nlohmann::json& in,
		Engine::MeshRendererComponent& component) {

		component.mesh = Engine::ParseAssetID(in, "mesh");
		component.material = Engine::ParseAssetID(in, "material");
		Engine::ReadRenderCommonFields(in, component.layer, component.order,
			component.visible, component.blendMode, component.queue);
		component.enableZPrepass =
			in.value("enableZPrepass", component.enableZPrepass);
		Engine::ReadMeshRenderFlags(in, component.renderFlags);
		component.renderingLayerMask =
			in.value("renderingLayerMask",
				component.renderingLayerMask) &
			Engine::kRenderingLayerMaskBits;
	}

	void WriteMeshRendererSettings(nlohmann::json& out,
		const Engine::MeshRendererComponent& component) {

		out["mesh"] = Engine::ToAssetReferenceJson(component.mesh);
		out["material"] = Engine::ToAssetReferenceJson(component.material);
		Engine::WriteRenderCommonFields(out, component.layer, component.order,
			component.visible, component.blendMode, component.queue);
		out["enableZPrepass"] = component.enableZPrepass;
		Engine::WriteMeshRenderFlags(out, component.renderFlags);
		out["renderingLayerMask"] =
			component.renderingLayerMask &
			Engine::kRenderingLayerMaskBits;
	}
}

//============================================================================
//	MeshRendererComponent classMethods
//============================================================================
void Engine::MeshRendererComponent::OnAdded(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] MeshRendererComponent& component) {

	if (!world.HasBuffer<SubMeshMaterial>(entity)) {
		world.AddBuffer<SubMeshMaterial>(entity);
	}
}

void Engine::MeshRendererComponent::OnRemoved(ECSWorld& world, const Entity& entity) {

	// サブメッシュ編集列はMeshRenderer本体と同じ寿命で破棄する
	if (world.HasBuffer<SubMeshMaterial>(entity)) {
		world.RemoveBuffer<SubMeshMaterial>(entity);
	}
}

void Engine::MeshRendererComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] MeshRendererComponent& component) {
}

void Engine::MeshRendererComponent::ReleaseStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] MeshRendererComponent& component) {
}

void Engine::MeshRendererComponent::DeserializeECS(
	ECSWorld& world, const Entity& entity, const nlohmann::json& in,
	MeshRendererComponent& component) {

	ReadMeshRendererSettings(in, component);
	std::vector<SubMeshMaterial> subMeshes;
	if (in.contains("subMeshes") && in["subMeshes"].is_array()) {
		subMeshes.reserve(in["subMeshes"].size());
		for (const nlohmann::json& subMeshJson : in["subMeshes"]) {
			subMeshes.emplace_back(subMeshJson.get<SubMeshMaterial>());
		}
	}
	SetMeshSubMeshes(world, entity, subMeshes);
}

void Engine::MeshRendererComponent::SerializeECS(
	const ECSWorld& world, const Entity& entity,
	const MeshRendererComponent& component, nlohmann::json& out) {

	SerializeMeshRenderer(component, GetMeshSubMeshes(world, entity), out);
}

void Engine::from_json(const nlohmann::json& in, SubMeshMaterial& subMeshMaterial) {

	subMeshMaterial.name = in.value("name", "");
	const std::string stableID = in.value("stableID", "");
	subMeshMaterial.stableID = stableID.empty() ? UUID{} : FromString16Hex(stableID);
	subMeshMaterial.sourceSubMeshIndex = in.value("sourceSubMeshIndex", 0u);

	// reflection駆動のパラメータ上書きを読む
	ReadMaterialInstance(
		in.value("materialInstance", nlohmann::json::array()),
		subMeshMaterial.materialInstance);
	subMeshMaterial.uvPos = Vector2::FromJson(in.value("uvPos", nlohmann::json{}));
	subMeshMaterial.uvRotation = in.value("uvRotation", 0.0f);
	subMeshMaterial.uvScale = Vector2::FromJson(in.value("uvScale", nlohmann::json{}));

	subMeshMaterial.localPos = Vector3::FromJson(in.value("localPos", nlohmann::json{}));
	subMeshMaterial.localRotation = Vector3::FromJson(in.value("localRotation", nlohmann::json{}));
	subMeshMaterial.localScale = Vector3::FromJson(in.value("localScale", nlohmann::json{}));

	subMeshMaterial.sourcePivot = Vector3::FromJson(in.value("sourcePivot", nlohmann::json{}));
}

void Engine::to_json(nlohmann::json& out, const SubMeshMaterial& subMeshMaterial) {

	out["name"] = subMeshMaterial.name;
	out["stableID"] = subMeshMaterial.stableID ? ToString(subMeshMaterial.stableID) : "";
	out["sourceSubMeshIndex"] = subMeshMaterial.sourceSubMeshIndex;

	// reflection駆動のパラメータ上書きを書き出す
	out["materialInstance"] =
		WriteMaterialInstance(subMeshMaterial.materialInstance);

	out["uvPos"] = subMeshMaterial.uvPos.ToJson();
	out["uvRotation"] = subMeshMaterial.uvRotation;
	out["uvScale"] = subMeshMaterial.uvScale.ToJson();

	out["localPos"] = subMeshMaterial.localPos.ToJson();
	out["localRotation"] = subMeshMaterial.localRotation.ToJson();
	out["localScale"] = subMeshMaterial.localScale.ToJson();

	out["sourcePivot"] = subMeshMaterial.sourcePivot.ToJson();
}

void Engine::ReadMeshRenderFlags(const nlohmann::json& in, MeshRenderFlags& flags) {

	const auto readFlag = [&](const char* key, MeshRenderFlags flag) {
		SetMeshRenderFlag(flags, flag, in.value(key, HasMeshRenderFlag(flags, flag)));
		};
	readFlag("lighting", MeshRenderFlags::Lighting);
	readFlag("castShadow", MeshRenderFlags::CastShadow);
	readFlag("receiveShadow", MeshRenderFlags::ReceiveShadow);
	readFlag("receiveIBL", MeshRenderFlags::ReceiveIBL);
	readFlag("castReflection", MeshRenderFlags::CastReflection);
	readFlag("receiveReflection", MeshRenderFlags::ReceiveReflection);
}

void Engine::WriteMeshRenderFlags(nlohmann::json& out, MeshRenderFlags flags) {

	out["lighting"] = HasMeshRenderFlag(flags, MeshRenderFlags::Lighting);
	out["castShadow"] = HasMeshRenderFlag(flags, MeshRenderFlags::CastShadow);
	out["receiveShadow"] = HasMeshRenderFlag(flags, MeshRenderFlags::ReceiveShadow);
	out["receiveIBL"] = HasMeshRenderFlag(flags, MeshRenderFlags::ReceiveIBL);
	out["castReflection"] = HasMeshRenderFlag(flags, MeshRenderFlags::CastReflection);
	out["receiveReflection"] = HasMeshRenderFlag(flags, MeshRenderFlags::ReceiveReflection);
}

void Engine::from_json(const nlohmann::json& in, MeshRendererComponent& component) {

	ReadMeshRendererSettings(in, component);
}

void Engine::to_json(nlohmann::json& out, const MeshRendererComponent& component) {

	WriteMeshRendererSettings(out, component);
}

std::span<Engine::SubMeshMaterial> Engine::GetMeshSubMeshes(
	ECSWorld& world, const Entity& entity) {

	return world.TryGetBuffer<SubMeshMaterial>(entity).GetSpan();
}

std::span<const Engine::SubMeshMaterial> Engine::GetMeshSubMeshes(
	const ECSWorld& world, const Entity& entity) {

	return world.GetBufferSpan<SubMeshMaterial>(entity);
}

bool Engine::SetMeshSubMesh(ECSWorld& world, const Entity& entity,
	uint32_t subMeshIndex, const SubMeshMaterial& subMesh) {

	DynamicBuffer<SubMeshMaterial> buffer =
		world.TryGetBuffer<SubMeshMaterial>(entity);
	if (!buffer.IsValid() || buffer.GetSize() <= subMeshIndex) {
		return false;
	}

	buffer.GetSpan()[subMeshIndex] = subMesh;
	// Buffer要素の変更は構造変更を伴わないため明示的に通知する
	world.MarkComponentModified<SubMeshMaterial>(entity);
	return true;
}

void Engine::SetMeshSubMeshes(ECSWorld& world, const Entity& entity,
	std::span<const SubMeshMaterial> subMeshes) {

	DynamicBuffer<SubMeshMaterial> buffer =
		world.TryGetBuffer<SubMeshMaterial>(entity);
	if (!buffer.IsValid()) {
		buffer = world.AddBuffer<SubMeshMaterial>(entity);
	}
	buffer.Clear();
	buffer.Reserve(static_cast<uint32_t>(subMeshes.size()));
	for (const SubMeshMaterial& subMesh : subMeshes) {
		buffer.Add(subMesh);
	}
	// Buffer要素の変更は構造変更を伴わないため明示的に通知する
	world.MarkComponentModified<SubMeshMaterial>(entity);
}

void Engine::SerializeMeshRenderer(
	const MeshRendererComponent& component,
	std::span<const SubMeshMaterial> subMeshes, nlohmann::json& out) {

	WriteMeshRendererSettings(out, component);
	out["subMeshes"] = nlohmann::json::array();
	for (const SubMeshMaterial& subMesh : subMeshes) {

		out["subMeshes"].push_back(subMesh);
	}
}

Engine::Matrix4x4 Engine::MeshSubMeshRuntime::BuildUVMatrix(
	const SubMeshMaterial& subMesh) {

	Vector3 scale(subMesh.uvScale.x, subMesh.uvScale.y, 1.0f);
	Vector3 rotation(0.0f, 0.0f, subMesh.uvRotation);
	Vector3 pos(subMesh.uvPos.x, subMesh.uvPos.y, 0.0f);
	return Matrix4x4::MakeAffineMatrix(scale, rotation, pos);
}

Engine::Matrix4x4 Engine::MeshSubMeshRuntime::BuildLocalMatrix(
	const SubMeshMaterial& subMesh) {

	return Matrix4x4::MakeAffineMatrix(subMesh.localScale, subMesh.localRotation, subMesh.localPos);
}

Engine::Matrix4x4 Engine::MeshSubMeshRuntime::BuildRenderLocalMatrix(const SubMeshMaterial& subMesh) {

	Matrix4x4 pivot = Matrix4x4::MakeTranslateMatrix(subMesh.sourcePivot);
	Matrix4x4 invPivot = Matrix4x4::MakeTranslateMatrix(Vector3(
		-subMesh.sourcePivot.x, -subMesh.sourcePivot.y, -subMesh.sourcePivot.z));
	return invPivot * BuildLocalMatrix(subMesh) * pivot;
}
