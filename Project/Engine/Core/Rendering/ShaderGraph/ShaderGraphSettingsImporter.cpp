#include "ShaderGraphSettingsImporter.h"
#include "ShaderGraphImportUtility.h"
#include "ShaderGraphPBRBuilder.h"
#include "ShaderGraphParameterDefaults.h"
#include "ShaderGraphSubGraphExpander.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>

// c++
#include <algorithm>
#include <exception>

namespace {

	using namespace Engine;
	using namespace Engine::ShaderGraphImportUtility;
	using Engine::UUID;
	using Kind = ShaderGraphNodeKind;
	using Type = ShaderGraphValueType;

	// 依存アセットを型指定で読み込む
	template <class T>
	T Read(AssetID id, AssetType type, const ShaderGraphImportResolver& resolver) {

		nlohmann::json data;
		T value{};
		Require(id && resolver(id, type, data) && FromJson(data, value), "取り込み元または依存アセットを読み込めません");
		return value;
	}

	// サンプラーの固定設定をノードへ写す
	PipelineStaticSamplerSettings CopySampler(const D3D12_STATIC_SAMPLER_DESC& source) {

		PipelineStaticSamplerSettings result;
		result.filter = source.Filter;
		result.addressU = source.AddressU;
		result.addressV = source.AddressV;
		result.addressW = source.AddressW;
		result.borderColor = source.BorderColor;
		result.comparisonFunc = source.ComparisonFunc;
		result.maxAnisotropy = source.MaxAnisotropy;
		result.mipLODBias = source.MipLODBias;
		result.minLOD = source.MinLOD;
		result.maxLOD = source.MaxLOD;
		return result;
	}

	// 標準パイプラインとの差分から表現可能な設定だけを抽出する
	PipelineStaticSamplerSettings ImportPipeline(ShaderGraphAsset& graph, const MaterialPassBinding& pass, AssetID standardID,
		const ShaderGraphImportResolver& resolver) {

		const auto pipeline = Read<RenderPipelineAsset>(pass.pipeline, AssetType::RenderPipeline, resolver);
		const auto standard = Read<RenderPipelineAsset>(standardID, AssetType::RenderPipeline, resolver);
		const auto find = [](const RenderPipelineAsset& asset, PipelineVariantKind kind) {
			return std::find_if(
				asset.variants.begin(), asset.variants.end(), [kind](const auto& variant) { return variant.kind == kind; });
		};
		const auto selected = find(pipeline, pass.preferredVariant);
		Require(selected != pipeline.variants.end(), "指定のパイプラインバリアントがありません");
		Require(!selected->staticSamplers.empty(), "サンプラー設定がありません");
		for (const auto& variant : pipeline.variants) {
			const auto base = find(standard, variant.kind);
			Require(base != standard.variants.end(), "未対応のパイプラインバリアントです");
			Require((pass.shaderOverride ? pass.shaderOverride : variant.shader) == base->shader,
				"独自シェーダーをノードへ変換できません");
			Require(variant.staticSamplers.size() == 1 && variant.staticSamplers.front().ShaderRegister == 0 &&
						variant.staticSamplers.front().RegisterSpace == 0,
				"標準Mesh PBRのサンプラー構成ではありません");
			// 対応する固定機能以外に差がある場合は取り込まない
			auto normalized = variant;
			normalized.rasterizer.FillMode = base->rasterizer.FillMode;
			normalized.rasterizer.CullMode = base->rasterizer.CullMode;
			normalized.rasterizer.FrontCounterClockwise = base->rasterizer.FrontCounterClockwise;
			normalized.rasterizer.DepthClipEnable = base->rasterizer.DepthClipEnable;
			normalized.depthStencil.DepthEnable = base->depthStencil.DepthEnable;
			normalized.depthStencil.DepthWriteMask = base->depthStencil.DepthWriteMask;
			normalized.depthStencil.DepthFunc = base->depthStencil.DepthFunc;
			normalized.depthStencil.StencilEnable = base->depthStencil.StencilEnable;
			normalized.staticSamplers = base->staticSamplers;
			RenderPipelineAsset left, right;
			left.variants = {normalized};
			right.variants = {*base};
			Require(ToJson(left) == ToJson(right), "ShaderGraphで再現できないパイプライン設定があります");
			Require(variant.rasterizer.FillMode == selected->rasterizer.FillMode &&
						variant.rasterizer.CullMode == selected->rasterizer.CullMode &&
						variant.rasterizer.FrontCounterClockwise == selected->rasterizer.FrontCounterClockwise &&
						variant.rasterizer.DepthClipEnable == selected->rasterizer.DepthClipEnable &&
						variant.depthStencil.DepthEnable == selected->depthStencil.DepthEnable &&
						variant.depthStencil.DepthWriteMask == selected->depthStencil.DepthWriteMask &&
						variant.depthStencil.DepthFunc == selected->depthStencil.DepthFunc &&
						variant.depthStencil.StencilEnable == selected->depthStencil.StencilEnable,
				"バリアント間で描画設定が異なります");
			left.variants = {variant};
			right.variants = {*selected};
			Require(ToJson(left)["variants"][0]["staticSamplers"] == ToJson(right)["variants"][0]["staticSamplers"],
				"バリアント間でサンプラー設定が異なります");
		}
		auto& state = graph.renderState;
		state.fillMode = selected->rasterizer.FillMode;
		state.cullMode = selected->rasterizer.CullMode;
		state.twoSided = state.cullMode == D3D12_CULL_MODE_NONE;
		state.frontCounterClockwise = selected->rasterizer.FrontCounterClockwise;
		state.depthClipEnable = selected->rasterizer.DepthClipEnable;
		state.depthTest = selected->depthStencil.DepthEnable;
		state.depthWrite = selected->depthStencil.DepthWriteMask == D3D12_DEPTH_WRITE_MASK_ALL;
		state.depthFunc = selected->depthStencil.DepthFunc;
		state.stencilEnable = selected->depthStencil.StencilEnable;
		return CopySampler(selected->staticSamplers.front());
	}

	// Material値を安定IDと既存の名前解決で対応付ける
	void ApplyValues(ShaderGraphAsset& graph, const MaterialAsset& material, const ShaderGraphImportResolver& resolver) {

		auto expected = ShaderGraphArtifactCache::CreateMaterial(graph, material.shaderGraph);
		// 子Graphの固定初期値も対応可能な値として確認する
		auto expanded = graph;
		std::vector<ShaderGraphDiagnostic> diagnostics;
		Require(ShaderGraphSubGraphExpander::Expand(expanded,
			[&](AssetID id, ShaderGraphAsset& child) {
				child = Read<ShaderGraphAsset>(id, AssetType::ShaderGraph, resolver);
				return true;
			}, diagnostics), "参照先のSubGraphを展開できません");
		const auto defaults = ShaderGraphParameterDefaults::Build(expanded);
		ShaderGraphParameterDefaults::ApplyMissing(defaults, expected.parameters);
		for (const auto& record : material.parameters.GetRecords()) {
			const auto& item = record.namedValue;
			bool found = false;
			for (auto& parameter : graph.parameters) {
				const auto* value = material.parameters.Find(MaterialParameterID::FromUUID(parameter.id));
				if (!value) {
					value = material.parameters.FindByName(parameter.name);
				}
				if (!value && parameter.semantic != MaterialParameterSemantic::None) {
					value = material.parameters.Find(parameter.semantic);
				}
				if (value == &item.second) {
					parameter.defaultValue = ConvertValue(*value, parameter.type);
					found = true;
				}
			}
			for (auto& keyword : graph.keywords) {
				const auto* value = material.parameters.Find(MaterialParameterID::FromUUID(keyword.id));
				if (!value) {
					value = material.parameters.FindByName(keyword.name);
				}
				if (value != &item.second || !keyword.runtimeToggle) {
					continue;
				}
				const auto type = keyword.type == ShaderGraphKeywordType::Boolean ? Type::Boolean : Type::Integer;
				ConvertValue(*value, type);
				keyword.defaultIndex = type == Type::Boolean ? std::get<bool>(value->value) : std::get<int32_t>(value->value);
				Require(type == Type::Boolean || keyword.defaultIndex < keyword.entries.size(), "Keywordの値が範囲外です");
				found = true;
			}
			// 生成Materialに含まれる固定の既定値はノードとは別に維持される
			const auto* fixed = expected.parameters.Find(record.id);
			MaterialParameterSet expectedValue, actualValue;
			if (fixed) {
				expectedValue.Set(
					MaterialParameterID::FromName(item.first), item.first, MaterialParameterSemantic::None, *fixed);
			}
			actualValue.Set(
				MaterialParameterID::FromName(item.first), item.first, MaterialParameterSemantic::None, item.second);
			Require(found || (fixed && expectedValue.GetContentHash() == actualValue.GetContentHash()),
				"グラフへ対応付けられないMaterialパラメータです: " + item.first);
		}
	}

	// 派生パス以外をコピーして独自処理が失われることを防ぐ
	void ValidateGraphPasses(
		ShaderGraphAsset& graph, const MaterialAsset& material, const ShaderGraphImportResolver& resolver) {

		auto expected = ShaderGraphArtifactCache::CreateMaterial(graph, material.shaderGraph);
		const auto uncompiled = expected;
		const auto artifact = ShaderGraphArtifactCache::DescribeReferences(graph, material.shaderGraph);
		ShaderGraphArtifactCache::ApplyToMaterial(artifact, expected);
		Require(material.passes.size() == expected.passes.size(), "独自の追加パスは取り込めません");
		for (const auto& pass : material.passes) {
			const auto* original = FindPass(uncompiled, pass.passKind);
			if (original && pass.pipeline == original->pipeline && !pass.shaderOverride &&
				pass.preferredVariant == original->preferredVariant) {
				continue;
			}
			const auto* base = FindPass(expected, pass.passKind);
			Require(base && pass.shaderOverride == base->shaderOverride && pass.preferredVariant == base->preferredVariant,
				"グラフ生成後に変更された独自パイプラインは取り込めません");
			if (pass.pipeline == base->pipeline) {
				continue;
			}
			const auto activeKind = material.renderState.surfaceMode == MaterialSurfaceMode::Transparent
										? MaterialPassKind::Transparent
										: MaterialPassKind::Draw;
			Require(graph.target == ShaderGraphTarget::Mesh && pass.passKind == activeKind &&
						graph.domain == ShaderGraphDomain::Surface,
				"実効Mesh描画パス以外の独自パイプラインは取り込めません");
			Require(std::none_of(graph.nodes.begin(), graph.nodes.end(),
						[](const auto& node) {
							return node.kind == Kind::SamplerState || node.kind == Kind::SubGraph ||
								   node.kind == Kind::CustomFunction;
						}),
				"独自パイプラインのサンプラーを既存ノードへ対応付けられません");
			auto pipelinePass = pass;
			pipelinePass.shaderOverride = {};
			const auto sampler = ImportPipeline(graph, pipelinePass,
				activeKind == MaterialPassKind::Transparent ? BuiltinAssets::Pipelines::DefaultMeshTransparent
															: BuiltinAssets::Pipelines::DefaultMesh,
				resolver);
			const auto samplerID = UUID::New();
			for (const auto& node : graph.nodes) {
				if (node.kind == Kind::TextureSample) {
					graph.links.push_back(
						ShaderGraphLink{.id = UUID::New(), .outputNode = samplerID, .inputNode = node.id, .inputSlot = 2});
				}
			}
			graph.nodes.push_back(ShaderGraphNode{.id = samplerID, .kind = Kind::SamplerState, .sampler = sampler});
		}
	}

	// 参照の欠損を確認し、コンパイル用コピーだけにファイルパスを解決する
	void ResolveReferences(ShaderGraphAsset& graph, const ShaderGraphImportResolver& resolver) {

		nlohmann::json data;
		for (const auto& parameter : graph.parameters) {
			if (parameter.type != Type::Texture2D) {
				continue;
			}
			const auto value = ConvertValue(parameter.defaultValue, Type::Texture2D);
			const auto id = std::get<AssetID>(value.value);
			Require(!id || resolver(id, AssetType::Texture, data), "参照先のTextureが見つかりません: " + parameter.name);
		}
		for (auto& node : graph.nodes) {
			if (node.kind == Kind::CustomFunction && node.functionFileAsset) {
				Require(resolver(node.functionFileAsset, AssetType::Unknown, data) && data.contains("path"),
					"Custom Functionの参照先が見つかりません");
				node.functionFile = data.at("path").get<std::string>();
			}
			if (node.kind == Kind::SubGraph) {
				Require(resolver(node.subGraph, AssetType::ShaderGraph, data), "参照先のSubGraphが見つかりません");
			}
		}
	}
}

//============================================================================
//	ShaderGraphSettingsImporter classMethods
//============================================================================
bool Engine::ShaderGraphSettingsImporter::Import(const ShaderGraphAsset& destination, AssetID source, AssetType sourceType,
	const ShaderGraphImportResolver& resolver, ShaderGraphAsset& output, std::string& error) {

	error.clear();
	try {
		ShaderGraphAsset candidate;
		if (sourceType == AssetType::ShaderGraph) {
			candidate = Read<ShaderGraphAsset>(source, sourceType, resolver);
		} else {
			Require(sourceType == AssetType::Material, "MaterialまたはShaderGraphを指定してください");
			const auto material = Read<MaterialAsset>(source, sourceType, resolver);
			if (material.shaderGraph) {
				candidate = Read<ShaderGraphAsset>(material.shaderGraph, AssetType::ShaderGraph, resolver);
				ValidateGraphPasses(candidate, material, resolver);
				ApplyValues(candidate, material, resolver);
			} else {
				Require(material.domain == MaterialDomain::Surface && material.usage == MaterialUsage::Mesh,
					"通常Materialの取り込みは標準Mesh PBRのみ対応しています");
				const bool transparent = material.renderState.surfaceMode == MaterialSurfaceMode::Transparent;
				const auto kind =
					transparent ? MaterialPassKind::Transparent
								: (material.renderState.surfaceMode == MaterialSurfaceMode::Masked ? MaterialPassKind::Masked
																								   : MaterialPassKind::Draw);
				const auto* pass = FindPass(material, kind);
				Require(pass != nullptr, "実効描画パスがありません");
				const auto standardID = transparent
											? BuiltinAssets::Pipelines::DefaultMeshTransparent
											: (kind == MaterialPassKind::Masked ? BuiltinAssets::Pipelines::DefaultMeshMasked
																				: BuiltinAssets::Pipelines::DefaultMesh);
				const auto sampler = ImportPipeline(candidate, *pass, standardID, resolver);
				const auto defaults = Read<MaterialAsset>(BuiltinAssets::Materials::DefaultMesh, AssetType::Material, resolver);
				for (const auto& binding : material.passes) {
					if (binding.passKind == kind) {
						continue;
					}
					const auto* base = FindPass(defaults, binding.passKind);
					Require(base && binding.pipeline == base->pipeline && binding.shaderOverride == base->shaderOverride,
						"独自の追加パスは取り込めません");
				}
				const auto state = candidate.renderState;
				candidate = ShaderGraphPBRBuilder::Build(material, defaults, sampler);
				candidate.renderState = state;
			}
			if (material.renderState.overridesRenderer) {
				Require(material.renderState.surfaceMode != MaterialSurfaceMode::Auto, "表面方式Autoは取り込めません");
				Require(
					candidate.domain != ShaderGraphDomain::Surface ||
						material.renderState.phase ==
							(IsShaderGraph3DTarget(candidate.target)
									? (material.renderState.surfaceMode == MaterialSurfaceMode::Transparent
											  ? RenderPhase::Transparent
											  : RenderPhase::Opaque)
									: ResolveMaterialRenderPhase(material.renderState.surfaceMode, material.renderState.phase)),
					"独自の描画フェーズは取り込めません");
				candidate.surfaceMode = material.renderState.surfaceMode == MaterialSurfaceMode::Transparent
											? ShaderGraphSurfaceMode::Transparent
											: ShaderGraphSurfaceMode::Opaque;
				candidate.renderState.alphaClipping = material.renderState.surfaceMode == MaterialSurfaceMode::Masked;
				candidate.renderState.blendMode = material.renderState.blendMode;
				candidate.renderState.castShadows = material.renderState.castShadows;
				candidate.renderState.receiveShadows = material.renderState.receiveShadows;
			}
		}
		candidate.name = destination.name;
		auto compileGraph = candidate;
		ResolveReferences(compileGraph, resolver);
		const auto compiled =
			ShaderGraphCompiler::Compile(compileGraph, "import.surface.hlsli", [&](AssetID id, ShaderGraphAsset& graph) {
				graph = Read<ShaderGraphAsset>(id, AssetType::ShaderGraph, resolver);
				ResolveReferences(graph, resolver);
				return true;
			});
		Require(compiled.Succeeded(), "取り込むグラフのノード構成をコンパイルできません");
		output = std::move(candidate);
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
}
