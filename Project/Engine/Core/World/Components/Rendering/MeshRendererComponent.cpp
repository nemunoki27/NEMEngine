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

	// reflection駆動のパラメータ上書きを読む
	subMeshMaterial.parameterOverrides.clear();
	if (in.contains("parameterOverrides") && in["parameterOverrides"].is_object()) {
		for (auto it = in["parameterOverrides"].begin(); it != in["parameterOverrides"].end(); ++it) {

			MaterialParameterValue value{};
			if (ParseMaterialParameterValue(it.value(), value)) {
				subMeshMaterial.parameterOverrides[it.key()] = std::move(value);
			}
		}
	}
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

	// reflection駆動のパラメータ上書きを書き出す
	out["parameterOverrides"] = nlohmann::json::object();
	for (const auto& [name, value] : subMeshMaterial.parameterOverrides) {

		out["parameterOverrides"][name] = SerializeMaterialParameterValue(value);
	}

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

	component.mesh = ParseAssetID(in, "mesh");
	component.material = ParseAssetID(in, "material");
	ReadRenderCommonFields(in, component.layer, component.order, component.visible, component.blendMode, component.queue);
	component.enableZPrepass = in.value("enableZPrepass", component.enableZPrepass);
	ReadMeshRenderFlags(in, component.renderFlags);

	component.subMeshes.clear();
	if (in.contains("subMeshes") && in["subMeshes"].is_array()) {
		for (const auto& subMeshJson : in["subMeshes"]) {

			component.subMeshes.emplace_back(subMeshJson.get<SubMeshMaterial>());
		}
	}
}

void Engine::to_json(nlohmann::json& out, const MeshRendererComponent& component) {

	out["mesh"] = ToAssetReferenceJson(component.mesh);
	out["material"] = ToAssetReferenceJson(component.material);
	WriteRenderCommonFields(out, component.layer, component.order, component.visible, component.blendMode, component.queue);
	out["enableZPrepass"] = component.enableZPrepass;
	WriteMeshRenderFlags(out, component.renderFlags);

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
