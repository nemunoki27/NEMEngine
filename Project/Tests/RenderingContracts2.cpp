#include "TestContracts.h"
#include "TestFixtures.h"
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Scripting/Managed/Generated/ManagedComponentBindings.generated.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptUtility.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineShapeBuilder.h>
#include <Engine/Core/Rendering/Textures/TextureImportSettings.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Rendering/Pipelines/Stage/BlendState.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingMaterialResolver.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Shaders/ShaderCookStorage.h>
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>
#include <Engine/Core/World/Components/Rendering/ScreenSpaceOutlineComponent.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

namespace NEMTests {

	bool TestScreenSpaceOutlineBinding() {

		using namespace Engine;
		using namespace Engine::GeneratedComponentBindings;
		ECSWorld world(ECSWorldKind::Runtime);
		auto& registry = ManagedWorldRegistry::GetInstance();
		const ManagedWorldHandle handle = registry.Register(world);
		const Entity entity = SceneAuthoring::CreateGameObject(world, "OutlineBinding");
		world.AddComponent<ScreenSpaceOutlineComponent>(entity);
		const ManagedNativeEntity native = MakeNativeEntity(world, entity);
		constexpr int32_t typeID = 24;
		constexpr int32_t enabledProperty = 0;
		constexpr int32_t colorProperty = 1;
		bool passed = true;

		for (float alpha : { 1.0f, 0.5f, 0.0f, 1.0f }) {
			const Color4 color(0.25f, 0.5f, 0.75f, alpha);
			Color4 restored{};
			const uint64_t revision = world.GetRenderDataRevision();
			passed &= SetComponentProperty(native, typeID, colorProperty, &color, sizeof(color)) == ManagedStatus::Ok;
			passed &= world.GetRenderDataRevision() > revision;
			passed &= GetComponentProperty(native, typeID, colorProperty, &restored, sizeof(restored)) == ManagedStatus::Ok;
			passed &= std::memcmp(&color, &restored, sizeof(color)) == 0;
		}

		for (int32_t enabled : { 0, 1 }) {
			int32_t restored = -1;
			passed &= SetComponentProperty(native, typeID, enabledProperty, &enabled, sizeof(enabled)) == ManagedStatus::Ok;
			passed &= GetComponentProperty(native, typeID, enabledProperty, &restored, sizeof(restored)) == ManagedStatus::Ok;
			passed &= restored == enabled;
		}

		// 不正な書き込みで既存の色を壊さない
		const Color4 color = world.GetComponent<ScreenSpaceOutlineComponent>(entity).color;
		Color4 restored{};
		passed &= SetComponentProperty(native, typeID, colorProperty, &restored, 4) == ManagedStatus::InvalidArgument;
		passed &= GetComponentProperty(native, typeID, colorProperty, &restored, sizeof(restored)) == ManagedStatus::Ok;
		passed &= std::memcmp(&color, &restored, sizeof(color)) == 0;
		passed &= GetComponentProperty(native, typeID, 99, &restored, sizeof(restored)) == ManagedStatus::InvalidArgument;

		// コンポーネントの再追加後もハンドルから引き直す
		world.RemoveComponent<ScreenSpaceOutlineComponent>(entity);
		passed &= GetComponentProperty(native, typeID, colorProperty, &restored, sizeof(restored)) == ManagedStatus::InvalidArgument;
		world.AddComponent<ScreenSpaceOutlineComponent>(entity);
		passed &= SetComponentProperty(native, typeID, colorProperty, &color, sizeof(color)) == ManagedStatus::Ok;
		registry.Unregister(handle);
		passed &= GetComponentProperty(native, typeID, colorProperty, &restored, sizeof(restored)) != ManagedStatus::Ok;
		return passed;
	}

	bool TestScreenSpaceOutlineSerialization() {

		Engine::ScreenSpaceOutlineComponent source{};
		source.alphaSource =
			Engine::ScreenSpaceOutlineAlphaSource::TextureColor;
		source.uiOcclusionMode =
			Engine::ScreenSpaceOutlineUIOcclusionMode::AlwaysVisible;

		nlohmann::json serialized{};
		Engine::to_json(serialized, source);
		Engine::ScreenSpaceOutlineComponent restored{};
		Engine::from_json(serialized, restored);

		return serialized.value("alphaSource", std::string{}) ==
			"TextureColor" &&
			serialized.value("uiOcclusionMode", std::string{}) ==
				"AlwaysVisible" &&
			restored.alphaSource ==
				Engine::ScreenSpaceOutlineAlphaSource::TextureColor &&
			restored.uiOcclusionMode ==
				Engine::ScreenSpaceOutlineUIOcclusionMode::AlwaysVisible;
	}

	bool TestTextureImportSettings() {

		const Engine::TextureImportSettings color =
			Engine::MakeTextureImportSettings(Engine::TextureImportPreset::Color);
		if (color.colorSpace != Engine::TextureColorSpace::SRGB ||
			color.filter != Engine::TextureFilterMode::Anisotropic ||
			!color.generateMipmaps || !color.alphaColorBleed) {
			return false;
		}

		const Engine::TextureImportSettings normal =
			Engine::MakeTextureImportSettings(Engine::TextureImportPreset::NormalMap);
		if (normal.colorSpace != Engine::TextureColorSpace::Linear ||
			normal.alphaColorBleed ||
			Engine::ToD3D12Filter(normal) != D3D12_FILTER_ANISOTROPIC) {
			return false;
		}

		const nlohmann::json data = {
			{ "preset", "Data" },
			{ "filter", "Point" },
			{ "addressU", "Mirror" },
			{ "maxAnisotropy", 99 },
		};
		const Engine::TextureImportSettings parsed =
			Engine::ParseTextureImportSettings(data);
		if (parsed.preset != Engine::TextureImportPreset::Data ||
			parsed.colorSpace != Engine::TextureColorSpace::Linear ||
			parsed.filter != Engine::TextureFilterMode::Point ||
			parsed.addressU != Engine::TextureAddressMode::Mirror ||
			parsed.maxAnisotropy != 16 ||
			Engine::ToD3D12AddressMode(parsed.addressU) !=
				D3D12_TEXTURE_ADDRESS_MODE_MIRROR) {
			return false;
		}

		if (Engine::ParseTextureImportSettings(Engine::ToJson(parsed)) != parsed) {
			return false;
		}

		const Engine::TextureImportSettings automatic{};
		return Engine::ResolveTextureColorSpace(
			automatic, Engine::TextureColorSpace::SRGB) ==
				Engine::TextureColorSpace::SRGB &&
			Engine::ResolveTextureColorSpace(
				normal, Engine::TextureColorSpace::SRGB) ==
					Engine::TextureColorSpace::Linear &&
			Engine::HashTextureImportSettings(
				automatic, Engine::TextureColorSpace::SRGB) !=
				Engine::HashTextureImportSettings(
					automatic, Engine::TextureColorSpace::Linear);
	}

	bool TestShaderReflectionMerge() {

		Engine::ShaderConstantBufferVariable vertexVariable{};
		vertexVariable.name = "metallic";
		vertexVariable.parameterID =
			Engine::MaterialParameterID::FromName(
				vertexVariable.name);
		vertexVariable.semantic =
			Engine::MaterialParameterSemantic::Metallic;
		vertexVariable.used = false;

		Engine::ShaderConstantBufferInfo vertexBuffer{};
		vertexBuffer.name = "MaterialParameters";
		vertexBuffer.bindPoint = 3;
		vertexBuffer.size = 16;
		vertexBuffer.variables.emplace_back(vertexVariable);
		Engine::ShaderReflectionInfo reflection{};
		reflection.constantBuffers.emplace_back(vertexBuffer);

		Engine::ShaderConstantBufferVariable pixelVariable =
			vertexVariable;
		pixelVariable.used = true;
		Engine::ShaderConstantBufferInfo pixelBuffer =
			vertexBuffer;
		pixelBuffer.variables.clear();
		pixelBuffer.variables.emplace_back(pixelVariable);
		Engine::ShaderReflectionInfo pixelReflection{};
		pixelReflection.constantBuffers.emplace_back(pixelBuffer);

		Engine::MergeShaderReflection(
			reflection, pixelReflection);
		const Engine::ShaderConstantBufferInfo* merged =
			Engine::FindConstantBuffer(
				reflection, "MaterialParameters");
		return merged && merged->variables.size() == 1 &&
			merged->variables.front().used;
	}
	bool TestLineShapeSegments() {

		using namespace Engine;
		const Vector3 origin(-2.0f, 3.0f, 5.0f);
		const Color4 color(0.2f, 0.4f, 0.6f, 0.8f);
		std::vector<LinePoint> points;

		// 矢印の輪郭と先端を確認
		LineShapeBuilder::BuildArrow(origin, 2.0f, Quaternion::Identity(), color, 0.75f, points);
		if (points.size() != 112) return false;
		const Vector3 tip = origin + Vector3(0.0f, 2.0f, 0.0f);
		size_t tipCount = 0;
		for (const LinePoint& point : points) {

			if (!std::isfinite(point.position.x) || !std::isfinite(point.position.y) ||
				!std::isfinite(point.position.z) || point.color != color || point.thickness != 0.75f ||
				point.position.y < origin.y || tip.y < point.position.y ||
				0.22001f < std::abs(point.position.x - origin.x) ||
				0.22001f < std::abs(point.position.z - origin.z)) return false;
			tipCount += point.position == tip ? 1 : 0;
		}
		if (tipCount != 4) return false;
		// 長さ0の追加で既存の点列を消さない
		LineShapeBuilder::BuildArrow(origin, 0.0f, Quaternion::Identity(), color, 0.75f, points);
		if (points.size() != 112) return false;

		// 軸の向きと色の対応を確認
		points.clear();
		LineShapeBuilder::BuildAxis(origin, Quaternion::Identity(), 2.0f, 0.5f, points);
		const std::array<Vector3, 3> ends = {
			origin + Vector3(2.0f, 0.0f, 0.0f), origin + Vector3(0.0f, 2.0f, 0.0f),
			origin + Vector3(0.0f, 0.0f, 2.0f) };
		const std::array<Color4, 3> colors = { Color4::Red(), Color4::Blue(), Color4::Green() };
		if (points.size() != 6) return false;
		for (size_t axis = 0; axis < 3; ++axis) {

			const LinePoint& start = points[axis * 2];
			const LinePoint& end = points[axis * 2 + 1];
			if (start.position != origin || end.position != ends[axis] ||
				start.color != colors[axis] || end.color != colors[axis] ||
				start.thickness != 0.5f || end.thickness != 0.5f) return false;
		}

		// 分割数0でも球面を有限の線分に揃える
		points.clear();
		LineShapeBuilder::BuildSphere(origin, 2.0f, color, 0, 0.75f, points);
		if (points.size() != 36) return false;
		for (const LinePoint& point : points) {

			if (!std::isfinite(point.position.x) || !std::isfinite(point.position.y) || !std::isfinite(point.position.z) ||
				std::abs((point.position - origin).Length() - 2.0f) > 0.0001f ||
				point.color != color || point.thickness != 0.75f) return false;
		}

		// 回転した半球の全点が指定した側に収まるか
		const Quaternion rotation = Quaternion::MakeAxisAngle(Vector3(0.0f, 0.0f, 1.0f), Math::pi / 2.0f);
		const Vector3 up = Vector3::TransferNormal(Vector3(0.0f, 1.0f, 0.0f), Quaternion::MakeRotateMatrix(rotation));
		points.clear();
		LineShapeBuilder::BuildHemisphere(origin, 2.0f, rotation, color, 4, 0.75f, points);
		if (points.size() != 64) return false;
		for (const LinePoint& point : points) {

			if (Vector3::Dot(point.position - origin, up) < -0.0001f ||
				std::abs((point.position - origin).Length() - 2.0f) > 0.0001f) return false;
		}

		// 箱の回転後も各軸の四辺の長さを維持
		points.clear();
		LineShapeBuilder::BuildOBB(origin, Vector3(1.0f, 2.0f, 3.0f), rotation, color, 0.75f, points);
		if (points.size() != 24) return false;
		std::array<size_t, 3> edgeCounts{};
		for (size_t index = 0; index < points.size(); index += 2) {

			const float length = (points[index + 1].position - points[index].position).Length();
			bool found = false;
			for (size_t axis = 0; axis < 3; ++axis) {

				if (std::abs(length - 2.0f * static_cast<float>(axis + 1)) < 0.0001f) {
					++edgeCounts[axis];
					found = true;
				}
			}
			if (!found) return false;
		}
		if (edgeCounts != std::array<size_t, 3>{ 4, 4, 4 }) return false;

		// 円錐台の輪郭が上下の指定半径に収まるか
		points.clear();
		LineShapeBuilder::BuildCone(origin, 2.0f, 1.0f, 3.0f, rotation, color, 4, 0.75f, points);
		if (points.size() != 24) return false;
		for (const LinePoint& point : points) {

			const Vector3 offset = point.position - origin;
			const float height = Vector3::Dot(offset, up);
			const bool base = std::abs(height) < 0.0001f;
			if (!base && std::abs(height - 3.0f) > 0.0001f) return false;
			if (std::abs((offset - up * height).Length() - (base ? 2.0f : 1.0f)) > 0.0001f) return false;
		}

		// 最小分割の円周も始点へ接続するか
		points.clear();
		LineShapeBuilder::BuildCircle2D(Vector2(origin.x, origin.y), 2.0f, color, 0, 0.75f, points);
		if (points.size() != 6 || points.front().position != points.back().position) return false;
		for (const LinePoint& point : points) {

			const Vector3 offset = point.position - Vector3(origin.x, origin.y, 0.0f);
			if (point.position.z != 0.0f || !std::isfinite(offset.Length()) ||
				std::abs(offset.Length() - 2.0f) > 0.0001f) return false;
		}

		// 矩形の回転後も指定サイズと閉じた四辺を維持
		points.clear();
		LineShapeBuilder::BuildRect2D(Vector2(origin.x, origin.y), Vector2(2.0f, 4.0f), rotation, color, 0.75f, points);
		if (points.size() != 8 || points.front().position != points.back().position) return false;
		for (const LinePoint& point : points) {

			if (std::abs(std::abs(point.position.x - origin.x) - 2.0f) > 0.0001f ||
				std::abs(std::abs(point.position.y - origin.y) - 1.0f) > 0.0001f || point.position.z != 0.0f) return false;
		}
		return true;
	}

	bool TestBlendStates() {

		struct ExpectedBlendState {

			Engine::BlendMode mode;
			D3D12_BLEND source;
			D3D12_BLEND destination;
			D3D12_BLEND_OP operation;
		};
		constexpr std::array expectedStates = {
			ExpectedBlendState{ Engine::BlendMode::Normal,
				D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA,
				D3D12_BLEND_OP_ADD },
			ExpectedBlendState{ Engine::BlendMode::Add,
				D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_ONE,
				D3D12_BLEND_OP_ADD },
			ExpectedBlendState{ Engine::BlendMode::Subtract,
				D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_ONE,
				D3D12_BLEND_OP_REV_SUBTRACT },
			ExpectedBlendState{ Engine::BlendMode::Multiply,
				D3D12_BLEND_ZERO, D3D12_BLEND_SRC_COLOR,
				D3D12_BLEND_OP_ADD },
			ExpectedBlendState{ Engine::BlendMode::Screen,
				D3D12_BLEND_INV_DEST_COLOR, D3D12_BLEND_ONE,
				D3D12_BLEND_OP_ADD },
			ExpectedBlendState{ Engine::BlendMode::Premultiplied,
				D3D12_BLEND_ONE, D3D12_BLEND_INV_SRC_ALPHA,
				D3D12_BLEND_OP_ADD },
		};
		for (const ExpectedBlendState& expected : expectedStates) {

			D3D12_RENDER_TARGET_BLEND_DESC desc{};
			Engine::BlendState{}.Create(expected.mode, desc);
			if (!desc.BlendEnable ||
				desc.SrcBlend != expected.source ||
				desc.DestBlend != expected.destination ||
				desc.BlendOp != expected.operation ||
				desc.SrcBlendAlpha != D3D12_BLEND_ONE ||
				desc.DestBlendAlpha != D3D12_BLEND_INV_SRC_ALPHA ||
				desc.BlendOpAlpha != D3D12_BLEND_OP_ADD) {

				return false;
			}
		}
		return true;
	}

}
