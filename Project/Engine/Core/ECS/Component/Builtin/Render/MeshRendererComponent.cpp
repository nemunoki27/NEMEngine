#include "MeshRendererComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/Enum/EnumAdapter.h>

//============================================================================
//	MeshRendererComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, MeshSubMeshTextureOverride& overrideData) {

	overrideData.name = in.value("name", "");
	const std::string stableID = in.value("stableID", "");
	overrideData.stableID = stableID.empty() ? UUID{} : FromString16Hex(stableID);
	overrideData.sourceSubMeshIndex = in.value("sourceSubMeshIndex", 0u);
	overrideData.baseColorTexture = ParseAssetID(in, "baseColorTexture");
	overrideData.normalTexture = ParseAssetID(in, "normalTexture");
	overrideData.metallicRoughnessTexture = ParseAssetID(in, "metallicRoughnessTexture");
	overrideData.specularTexture = ParseAssetID(in, "specularTexture");
	overrideData.emissiveTexture = ParseAssetID(in, "emissiveTexture");
	overrideData.occlusionTexture = ParseAssetID(in, "occlusionTexture");

	// サブメッシュパラメータ
	overrideData.color = Color4::FromJson(in.value("color", nlohmann::json{}));
	overrideData.uvPos = Vector2::FromJson(in.value("uvPos", nlohmann::json{}));
	overrideData.uvRotation = in.value("uvRotation", 0.0f);
	overrideData.uvScale = Vector2::FromJson(in.value("uvScale", nlohmann::json{}));

	overrideData.localPos = Vector3::FromJson(in.value("localPos", nlohmann::json{}));
	overrideData.localRotation = Vector3::FromJson(in.value("localRotation", nlohmann::json{}));
	overrideData.localScale = Vector3::FromJson(in.value("localScale", nlohmann::json{}));

	overrideData.uvMatrix = Matrix4x4::Identity();
	overrideData.worldMatrix = Matrix4x4::Identity();

	overrideData.sourcePivot = Vector3::FromJson(in.value("sourcePivot", nlohmann::json{}));
}

void Engine::to_json(nlohmann::json& out, const MeshSubMeshTextureOverride& overrideData) {

	out["name"] = overrideData.name;
	out["stableID"] = overrideData.stableID ? ToString(overrideData.stableID) : "";
	out["sourceSubMeshIndex"] = overrideData.sourceSubMeshIndex;
	out["baseColorTexture"] = ToString(overrideData.baseColorTexture);
	out["normalTexture"] = ToString(overrideData.normalTexture);
	out["metallicRoughnessTexture"] = ToString(overrideData.metallicRoughnessTexture);
	out["specularTexture"] = ToString(overrideData.specularTexture);
	out["emissiveTexture"] = ToString(overrideData.emissiveTexture);
	out["occlusionTexture"] = ToString(overrideData.occlusionTexture);

	// サブメッシュパラメータ
	out["color"] = overrideData.color.ToJson();

	out["uvPos"] = overrideData.uvPos.ToJson();
	out["uvRotation"] = overrideData.uvRotation;
	out["uvScale"] = overrideData.uvScale.ToJson();

	out["localPos"] = overrideData.localPos.ToJson();
	out["localRotation"] = overrideData.localRotation.ToJson();
	out["localScale"] = overrideData.localScale.ToJson();

	out["sourcePivot"] = overrideData.sourcePivot.ToJson();
}

void Engine::from_json(const nlohmann::json& in, MeshRendererComponent& component) {

	component.mesh = ParseAssetID(in, "mesh");
	component.material = ParseAssetID(in, "material");
	component.queue = in.value("queue", component.queue);
	component.layer = in.value("layer", component.layer);
	component.order = in.value("order", component.order);
	component.visible = in.value("visible", component.visible);
	component.enableZPrepass = in.value("enableZPrepass", component.enableZPrepass);
	component.blendMode = EnumAdapter<BlendMode>::FromString(in.value("blendMode", "Normal")).value();

	component.subMeshes.clear();
	if (in.contains("subMeshes") && in["subMeshes"].is_array()) {
		for (const auto& subMeshJson : in["subMeshes"]) {

			component.subMeshes.emplace_back(subMeshJson.get<MeshSubMeshTextureOverride>());
		}
	}
}

void Engine::to_json(nlohmann::json& out, const MeshRendererComponent& component) {

	out["mesh"] = ToString(component.mesh);
	out["material"] = ToString(component.material);
	out["queue"] = component.queue;
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
	const MeshSubMeshTextureOverride& subMesh) {

	Vector3 scale(subMesh.uvScale.x, subMesh.uvScale.y, 1.0f);
	Vector3 rotation(0.0f, 0.0f, subMesh.uvRotation);
	Vector3 pos(subMesh.uvPos.x, subMesh.uvPos.y, 0.0f);
	return Matrix4x4::MakeAffineMatrix(scale, rotation, pos);
}

Engine::Matrix4x4 Engine::MeshSubMeshRuntime::BuildLocalMatrix(
	const MeshSubMeshTextureOverride& subMesh) {

	return Matrix4x4::MakeAffineMatrix(subMesh.localScale, subMesh.localRotation, subMesh.localPos);
}

Engine::Matrix4x4 Engine::MeshSubMeshRuntime::BuildGizmoLocalMatrix(const MeshSubMeshTextureOverride& subMesh) {

	Matrix4x4 pivot = Matrix4x4::MakeTranslateMatrix(subMesh.sourcePivot);
	return BuildLocalMatrix(subMesh) * pivot;
}

Engine::Matrix4x4 Engine::MeshSubMeshRuntime::BuildRenderLocalMatrix(const MeshSubMeshTextureOverride& subMesh) {

	Matrix4x4 pivot = Matrix4x4::MakeTranslateMatrix(subMesh.sourcePivot);
	Matrix4x4 invPivot = Matrix4x4::MakeTranslateMatrix(Vector3(
		-subMesh.sourcePivot.x, -subMesh.sourcePivot.y, -subMesh.sourcePivot.z));
	return invPivot * BuildLocalMatrix(subMesh) * pivot;
}

void Engine::MeshSubMeshRuntime::UpdateSubMeshRuntime(
	MeshSubMeshTextureOverride& subMesh, const Matrix4x4& parentWorldMatrix) {

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