#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Textures/TextureAssetResolver.h>

#include <array>
#include <unordered_set>

namespace Engine::ShaderGraphNodePreviewUtility {

	enum class PreviewOperation : uint32_t {

		Value = 0,
		UV,
		WorldNormal,
		WorldPosition,
		Add,
		Subtract,
		Multiply,
		Divide,
		Power,
		Lerp,
		OneMinus,
		Saturate,
		Sine,
		Remap,
		TilingAndOffset,
		PolarCoordinates,
		Split,
		Combine,
		TextureSample,
		NormalUnpack,
		TextureValue,
		Time,
	};

	enum class PreviewSwizzle : uint32_t {

		Identity = 0,
		RGB,
		R,
		G,
		B,
		A,
		RG,
	};

	struct PreviewConstants {

		uint32_t operation = 0;
		uint32_t connectedMask = 0;
		uint32_t textureIndex = UINT32_MAX;
		uint32_t outputValueType = 0;

		Engine::Vector4 literalValue{};
		std::array<Engine::Vector4, 4> inputDefaults{};
		std::array<uint32_t, 4> inputSwizzles{};
		Engine::Vector4 timeValues{};
	};

	struct PreviewTextureReference {

		Engine::AssetID assetID{};
		bool sRGB = false;
	};

	// ノードプレビューの入力を解決する
uint64_t CalculatePreviewHash(
		const Engine::ShaderGraphAsset& graph);
	// ノードプレビューの入力を解決する
const Engine::ShaderGraphNode* FindPreviewNode(
		const Engine::ShaderGraphAsset& graph,
		Engine::UUID id);
	// ノードプレビューの入力を解決する
const Engine::ShaderGraphLink* FindPreviewInput(
		const Engine::ShaderGraphAsset& graph,
		const Engine::ShaderGraphNode& node,
		uint32_t slot);
	// ノードプレビューの入力を解決する
Engine::ShaderGraphValueType ResolvePreviewOutputType(
		const Engine::ShaderGraphAsset& graph,
		const Engine::ShaderGraphNode& node,
		uint32_t outputSlot,
		std::unordered_set<uint64_t>& visiting);
	// ノードプレビューの入力を解決する
Engine::ShaderGraphValueType ResolvePreviewOutputType(
		const Engine::ShaderGraphAsset& graph,
		const Engine::ShaderGraphNode& node,
		uint32_t outputSlot = 0);
	// ノードプレビューの入力を解決する
PreviewSwizzle ResolvePreviewSwizzle(
		const Engine::ShaderGraphAsset& graph,
		const Engine::ShaderGraphNode& source,
		uint32_t outputSlot);
	// ノードプレビューの入力を解決する
PreviewOperation GetPreviewOperation(
		const Engine::ShaderGraphAsset& graph,
		const Engine::ShaderGraphNode& node);
	// ノードプレビューの入力を解決する
bool IsPreviewableNode(
		Engine::ShaderGraphNodeKind kind);
	// ノードプレビューの入力を解決する
std::string PreviewTextureName(Engine::UUID nodeID);
	// ノードプレビューの入力を解決する
Engine::Vector4 ToPreviewVector(
		const Engine::MaterialParameterValue& parameter,
		Engine::ShaderGraphValueType type);
	// ノードプレビューの入力を解決する
Engine::Vector4 PreviewInputDefault(
		Engine::ShaderGraphNodeKind kind,
		uint32_t slot);
	// ノードプレビューの入力を解決する
PreviewTextureReference ResolvePreviewTextureReference(
		const Engine::ShaderGraphAsset& graph,
		const Engine::ShaderGraphNode& node);
	// ノードプレビューの入力を解決する
uint64_t CombinePreviewHash(
		uint64_t seed, uint64_t value);
	// ノードプレビューの入力を解決する
uint64_t HashPreviewValue(
		const Engine::MaterialParameterValue& parameter);
	// ノードプレビューの入力を解決する
uint32_t PreviewComponentCount(
		Engine::ShaderGraphValueType type);
	// ノードプレビューの入力を解決する
Engine::ShaderGraphValueType ResolvePreviewInputType(
		const Engine::ShaderGraphAsset& graph,
		const Engine::ShaderGraphNode& node,
		uint32_t inputSlot,
		Engine::ShaderGraphValueType fallback,
		std::unordered_set<uint64_t>& visiting);
	// 入力ノードから順に評価対象を並べる
	std::vector<const Engine::ShaderGraphNode*> BuildPreviewOrder(const Engine::ShaderGraphAsset& graph);
	// プレビューの参照パラメータを解決する
	const Engine::ShaderGraphParameter* FindPreviewParameter(const Engine::ShaderGraphAsset& graph, Engine::UUID id);
}
