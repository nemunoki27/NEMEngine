#include "MeshRendererComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	MeshRendererComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, SubMeshMaterial& subMeshMaterial) {

	subMeshMaterial.name = in.value("name", "");
	const std::string stableID = in.value("stableID", "");
	subMeshMaterial.stableID = stableID.empty() ? UUID{} : FromString16Hex(stableID);
	subMeshMaterial.sourceSubMeshIndex = in.value("sourceSubMeshIndex", 0u);
	subMeshMaterial.baseColorTexture = ParseAssetID(in, "baseColorTexture");
	subMeshMaterial.normalTexture = ParseAssetID(in, "normalTexture");
	subMeshMaterial.metallicRoughnessTexture = ParseAssetID(in, "metallicRoughnessTexture");
	subMeshMaterial.specularTexture = ParseAssetID(in, "specularTexture");
	subMeshMaterial.emissiveTexture = ParseAssetID(in, "emissiveTexture");
	subMeshMaterial.occlusionTexture = ParseAssetID(in, "occlusionTexture");

	// サブメッシュパラメータ
	subMeshMaterial.color = Color4::FromJson(in.value("color", nlohmann::json{}));
	subMeshMaterial.emissiveColor = Color4::FromJson(in.value("emissiveColor", nlohmann::json{}));
	subMeshMaterial.metallic = in.value("metallic", 0.0f);
	subMeshMaterial.roughness = in.value("roughness", 0.5f);
	subMeshMaterial.uvPos = Vector2::FromJson(in.value("uvPos", nlohmann::json{}));
	subMeshMaterial.uvRotation = in.value("uvRotation", 0.0f);
	subMeshMaterial.uvScale = Vector2::FromJson(in.value("uvScale", nlohmann::json{}));

	subMeshMaterial.localPos = Vector3::FromJson(in.value("localPos", nlohmann::json{}));
	subMeshMaterial.localRotation = Vector3::FromJson(in.value("localRotation", nlohmann::json{}));
	subMeshMaterial.localScale = Vector3::FromJson(in.value("localScale", nlohmann::json{}));

	subMeshMaterial.uvMatrix = Matrix4x4::Identity();
	subMeshMaterial.worldMatrix = Matrix4x4::Identity();

	subMeshMaterial.sourcePivot = Vector3::FromJson(in.value("sourcePivot", nlohmann::json{}));
}

void Engine::to_json(nlohmann::json& out, const SubMeshMaterial& subMeshMaterial) {

	out["name"] = subMeshMaterial.name;
	out["stableID"] = subMeshMaterial.stableID ? ToString(subMeshMaterial.stableID) : "";
	out["sourceSubMeshIndex"] = subMeshMaterial.sourceSubMeshIndex;
	out["baseColorTexture"] = ToString(subMeshMaterial.baseColorTexture);
	out["normalTexture"] = ToString(subMeshMaterial.normalTexture);
	out["metallicRoughnessTexture"] = ToString(subMeshMaterial.metallicRoughnessTexture);
	out["specularTexture"] = ToString(subMeshMaterial.specularTexture);
	out["emissiveTexture"] = ToString(subMeshMaterial.emissiveTexture);
	out["occlusionTexture"] = ToString(subMeshMaterial.occlusionTexture);

	// サブメッシュパラメータ
	out["color"] = subMeshMaterial.color.ToJson();
	out["emissiveColor"] = subMeshMaterial.emissiveColor.ToJson();
	out["metallic"] = subMeshMaterial.metallic;
	out["roughness"] = subMeshMaterial.roughness;
	out["uvPos"] = subMeshMaterial.uvPos.ToJson();
	out["uvRotation"] = subMeshMaterial.uvRotation;
	out["uvScale"] = subMeshMaterial.uvScale.ToJson();

	out["localPos"] = subMeshMaterial.localPos.ToJson();
	out["localRotation"] = subMeshMaterial.localRotation.ToJson();
	out["localScale"] = subMeshMaterial.localScale.ToJson();

	out["sourcePivot"] = subMeshMaterial.sourcePivot.ToJson();
}

void Engine::from_json(const nlohmann::json& in, MeshRendererComponent& component) {

	component.mesh = ParseAssetID(in, "mesh");
	component.material = ParseAssetID(in, "material");
	component.queue = RenderPhaseFromString(in.value("queue", std::string(ToString(component.queue))), component.queue);
	component.layer = in.value("layer", component.layer);
	component.order = in.value("order", component.order);
	component.visible = in.value("visible", component.visible);
	component.enableZPrepass = in.value("enableZPrepass", component.enableZPrepass);
	component.blendMode = EnumAdapter<BlendMode>::FromString(in.value("blendMode", "Normal")).value();

	component.subMeshes.clear();
	if (in.contains("subMeshes") && in["subMeshes"].is_array()) {
		for (const auto& subMeshJson : in["subMeshes"]) {

			component.subMeshes.emplace_back(subMeshJson.get<SubMeshMaterial>());
		}
	}
}

void Engine::to_json(nlohmann::json& out, const MeshRendererComponent& component) {

	out["mesh"] = ToString(component.mesh);
	out["material"] = ToString(component.material);
	out["queue"] = std::string(ToString(component.queue));
	out["layer"] = component.layer;
	out["order"] = component.order;
	out["visible"] = component.visible;
	out["enableZPrepass"] = component.enableZPrepass;
	out["blendMode"] = EnumAdapter<BlendMode>::ToString(component.blendMode);

	out["subMeshes"] = nlohmann::json::array();
	for (const auto& subMesh : component.subMeshes) {

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

Engine::Matrix4x4 Engine::MeshSubMeshRuntime::BuildGizmoLocalMatrix(const SubMeshMaterial& subMesh) {

	Matrix4x4 pivot = Matrix4x4::MakeTranslateMatrix(subMesh.sourcePivot);
	return BuildLocalMatrix(subMesh) * pivot;
}

Engine::Matrix4x4 Engine::MeshSubMeshRuntime::BuildRenderLocalMatrix(const SubMeshMaterial& subMesh) {

	Matrix4x4 pivot = Matrix4x4::MakeTranslateMatrix(subMesh.sourcePivot);
	Matrix4x4 invPivot = Matrix4x4::MakeTranslateMatrix(Vector3(
		-subMesh.sourcePivot.x, -subMesh.sourcePivot.y, -subMesh.sourcePivot.z));
	return invPivot * BuildLocalMatrix(subMesh) * pivot;
}

void Engine::MeshSubMeshRuntime::UpdateSubMeshRuntime(
	SubMeshMaterial& subMesh, const Matrix4x4& parentWorldMatrix) {

	subMesh.uvMatrix = BuildUVMatrix(subMesh);

	const Matrix4x4 localMatrix = BuildRenderLocalMatrix(subMesh);
	subMesh.worldMatrix = localMatrix * parentWorldMatrix;
}

void Engine::MeshSubMeshRuntime::UpdateRendererRuntime(
	MeshRendererComponent& renderer, const Matrix4x4& parentWorldMatrix) {

	for (auto& subMesh : renderer.subMeshes) {

		UpdateSubMeshRuntime(subMesh, parentWorldMatrix);
	}
}
