#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphPreviewFingerprint.h"

// c++
#include <array>
#include <cstdint>
#include <string>
#include <vector>
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

	// IDが一致するノードを取得する
	const Engine::ShaderGraphNode* FindPreviewNode(const Engine::ShaderGraphAsset& graph, Engine::UUID id);

	// 指定入力に接続したリンクを取得する
	const Engine::ShaderGraphLink* FindPreviewInput(
		const Engine::ShaderGraphAsset& graph, const Engine::ShaderGraphNode& node, uint32_t slot);

	// 接続をたどって出力の型を決める
	Engine::ShaderGraphValueType ResolvePreviewOutputType(const Engine::ShaderGraphAsset& graph,
		const Engine::ShaderGraphNode& node, uint32_t outputSlot, std::unordered_set<uint64_t>& visiting);

	// 接続をたどって出力の型を決める
	Engine::ShaderGraphValueType ResolvePreviewOutputType(
		const Engine::ShaderGraphAsset& graph, const Engine::ShaderGraphNode& node, uint32_t outputSlot = 0);

	// 出力スロットの成分選択を決める
	PreviewSwizzle ResolvePreviewSwizzle(
		const Engine::ShaderGraphAsset& graph, const Engine::ShaderGraphNode& source, uint32_t outputSlot);

	// ノードをプレビュー用の演算へ変換する
	PreviewOperation GetPreviewOperation(const Engine::ShaderGraphAsset& graph, const Engine::ShaderGraphNode& node);

	// プレビューで扱えるノードか確認する
	bool IsPreviewableNode(Engine::ShaderGraphNodeKind kind);

	// ノード専用の描画先名を作る
	std::string PreviewTextureName(Engine::UUID nodeID);

	// 設定値をプレビュー用の四成分へ変換する
	Engine::Vector4 ToPreviewVector(const Engine::MaterialParameterValue& parameter, Engine::ShaderGraphValueType type);

	// 未接続入力の既定値を取得する
	Engine::Vector4 PreviewInputDefault(Engine::ShaderGraphNodeKind kind, uint32_t slot);

	// Textureと色空間を解決する
	PreviewTextureReference ResolvePreviewTextureReference(
		const Engine::ShaderGraphAsset& graph, const Engine::ShaderGraphNode& node);

	// 数値型の成分数を取得する
	uint32_t PreviewComponentCount(Engine::ShaderGraphValueType type);

	// 入力の接続先か既定の型を取得する
	Engine::ShaderGraphValueType ResolvePreviewInputType(const Engine::ShaderGraphAsset& graph,
		const Engine::ShaderGraphNode& node, uint32_t inputSlot, Engine::ShaderGraphValueType fallback,
		std::unordered_set<uint64_t>& visiting);

	// 有効なNodeだけを依存順で並べる
	std::vector<const Engine::ShaderGraphNode*> BuildPreviewOrder(const Engine::ShaderGraphAsset& graph);

	// プレビューの参照パラメータを解決する
	const Engine::ShaderGraphParameter* FindPreviewParameter(const Engine::ShaderGraphAsset& graph, Engine::UUID id);
} // Engine::ShaderGraphNodePreviewUtility
