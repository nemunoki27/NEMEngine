#include "ShaderGraphSettingsImporter.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>

// c++
#include <algorithm>
#include <array>
#include <stdexcept>
#include <unordered_set>

namespace {

	using namespace Engine;
	using Engine::UUID;
	using Kind = ShaderGraphNodeKind;
	using Type = ShaderGraphValueType;

	// 変換不能な設定は候補グラフごと破棄する
	void Require(bool condition, const std::string& message) {

		if (!condition) {
			throw std::runtime_error(message);
		}
	}

	// 依存アセットを型指定で読み込む
	template<class T>
	T Read(AssetID id, AssetType type, const ShaderGraphImportResolver& resolver) {

		nlohmann::json data;
		T value{};
		Require(id && resolver(id, type, data) && FromJson(data, value),
			"取り込み元または依存アセットを読み込めません");
		return value;
	}

	// 既定値と保存値の型を合わせる
	MaterialParameterValue ConvertValue(const MaterialParameterValue& value, Type type) {

		bool valid = false;
		switch (type) {
		case Type::Float: valid = std::holds_alternative<float>(value.value); break;
		case Type::Float2: valid = std::holds_alternative<Vector2>(value.value); break;
		case Type::Float3: valid = std::holds_alternative<Vector3>(value.value); break;
		case Type::Float4: valid = std::holds_alternative<Vector4>(value.value); break;
		case Type::Color: valid = std::holds_alternative<Color4>(value.value); break;
		case Type::Texture2D: valid = std::holds_alternative<AssetID>(value.value); break;
		case Type::Boolean: valid = std::holds_alternative<bool>(value.value); break;
		case Type::Integer: valid = std::holds_alternative<int32_t>(value.value); break;
		default: break;
		}
		Require(valid, "パラメータ値の型をグラフへ変換できません");
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
	PipelineStaticSamplerSettings ImportPipeline(ShaderGraphAsset& graph,
		const MaterialPassBinding& pass, AssetID standardID,
		const ShaderGraphImportResolver& resolver) {

		const auto pipeline = Read<RenderPipelineAsset>(pass.pipeline, AssetType::RenderPipeline, resolver);
		const auto standard = Read<RenderPipelineAsset>(standardID, AssetType::RenderPipeline, resolver);
		const auto find = [](const RenderPipelineAsset& asset, PipelineVariantKind kind) {
			return std::find_if(asset.variants.begin(), asset.variants.end(),
				[kind](const auto& variant) { return variant.kind == kind; });
		};
		const auto selected = find(pipeline, pass.preferredVariant);
		Require(selected != pipeline.variants.end(), "指定のパイプラインバリアントがありません");
		Require(!selected->staticSamplers.empty(), "サンプラー設定がありません");
		for (const auto& variant : pipeline.variants) {
			const auto base = find(standard, variant.kind);
			Require(base != standard.variants.end(), "未対応のパイプラインバリアントです");
			Require((pass.shaderOverride ? pass.shaderOverride : variant.shader) == base->shader,
				"独自シェーダーをノードへ変換できません");
			Require(variant.staticSamplers.size() == 1 &&
				variant.staticSamplers.front().ShaderRegister == 0 &&
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
			left.variants = { normalized };
			right.variants = { *base };
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
			left.variants = { variant };
			right.variants = { *selected };
			Require(ToJson(left)["variants"][0]["staticSamplers"] ==
				ToJson(right)["variants"][0]["staticSamplers"], "バリアント間でサンプラー設定が異なります");
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

	struct Pin {
		UUID node{};
		uint32_t slot = 0;
	};

	// 標準PBRの式を編集可能なノードへ組み立てる
	class PBRGraphBuilder {
	public:
		ShaderGraphAsset graph;
		MaterialParameterSet values;
		std::unordered_set<std::string> consumed;
		UUID group{};
		Pin sampler{};
		float row = 0.0f;
		float column = 0.0f;

		Pin Node(Kind kind) {

			const auto id = UUID::New();
			graph.nodes.push_back(ShaderGraphNode{ .id = id, .groupID = group,
				.kind = kind, .position = Vector2(column, row), .previewExpanded = false });
			column += 220.0f;
			return { id, 0 };
		}

		void Link(Pin from, Pin to, uint32_t input) {

			graph.links.push_back(ShaderGraphLink{ .id = UUID::New(),
				.outputNode = from.node, .outputSlot = from.slot, .inputNode = to.node, .inputSlot = input });
		}

		Pin Constant(float value) {

			auto result = Node(Kind::Constant);
			graph.nodes.back().value.value = value;
			return result;
		}

		Pin Operation(Kind kind, Pin a, Pin b) {

			auto result = Node(kind);
			Link(a, result, 0);
			Link(b, result, 1);
			return result;
		}

		MaterialParameterValue Value(const std::string& name, MaterialParameterValue fallback) {

			consumed.insert(name);
			const auto semantic = ResolveMaterialParameterSemantic(name);
			const auto* value = values.FindByName(name);
			if (!value && semantic != MaterialParameterSemantic::None) {
				value = values.Find(semantic);
			}
			return value ? *value : fallback;
		}

		Pin Parameter(const std::string& name, Type type, MaterialParameterValue fallback) {

			const auto existing = std::find_if(graph.parameters.begin(), graph.parameters.end(),
				[&](const auto& parameter) { return parameter.name == name; });
			if (existing != graph.parameters.end()) {
				const auto node = std::find_if(graph.nodes.begin(), graph.nodes.end(),
					[&](const auto& value) { return value.kind == Kind::Parameter && value.parameterID == existing->id; });
				return { node->id, 0 };
			}
			auto result = Node(Kind::Parameter);
			const auto id = UUID::New();
			graph.nodes.back().parameterID = id;
			graph.parameters.push_back(ShaderGraphParameter{ .id = id, .name = name, .type = type,
				.semantic = ResolveMaterialParameterSemantic(name),
				.defaultValue = ConvertValue(Value(name, fallback), type), .referenceName = name });
			return result;
		}

		Pin Texture(const std::string& name, uint32_t slot, bool normal = false) {

			const auto value = ConvertValue(Value(name, { .value = AssetID{} }), Type::Texture2D);
			// 未設定でも編集用の入力を残し、サンプルの代替値で見た目を維持する
			const auto parameter = Parameter(name, Type::Texture2D, value);
			auto sample = Node(Kind::TextureSample);
			graph.nodes.back().value.value = normal ? Vector4(0.5f, 0.5f, 1.0f, 1.0f) : Vector4(1.0f, 1.0f, 1.0f, 1.0f);
			Link(parameter, sample, 0);
			Link(sampler, sample, 2);
			if (normal) {
				const auto unpack = Node(Kind::NormalUnpack);
				Link(sample, unpack, 0);
				return unpack;
			}
			sample.slot = slot;
			return sample;
		}

		void Begin(const std::string& name) {

			row += 260.0f;
			column = 40.0f;
			group = UUID::New();
			graph.groups.push_back(ShaderGraphGroup{ .id = group, .name = name,
				.position = Vector2(10.0f, row - 40.0f), .size = Vector2(2000.0f, 220.0f) });
		}
	};

	// ノード化できる標準PBRの値を取り込む
	ShaderGraphAsset BuildPBR(const MaterialAsset& material, const MaterialAsset& defaults,
		const PipelineStaticSamplerSettings& sampler) {

		PBRGraphBuilder builder;
		builder.values = defaults.parameters;
		builder.values.MergeFrom(material.parameters);
		const auto displacement = builder.Value("displacementScale", { .value = 0.0f });
		Require(std::get<float>(ConvertValue(displacement, Type::Float).value) == 0.0f,
			"Displacementが有効なMaterialは取り込めません");
		builder.Value("displacementMidpoint", { .value = 0.5f });
		builder.Value("displacementTexture", { .value = AssetID{} });
		builder.sampler = builder.Node(Kind::SamplerState);
		builder.graph.nodes.back().sampler = sampler;
		const auto output = builder.Node(Kind::SurfaceOutput);
		builder.graph.nodes.back().position = Vector2(2200.0f, 600.0f);
		builder.graph.outputNode = output.node;
		builder.Begin("ベースカラー");
		const auto color = builder.Parameter("color", Type::Color, { .value = Color4(1, 1, 1, 1) });
		builder.Link(builder.Operation(Kind::Multiply, color, builder.Texture("baseColorTexture", 0)), output, 0);
		builder.Begin("法線");
		builder.Link(builder.Texture("normalTexture", 0, true), output, 1);
		builder.Begin("メタリック");
		const auto metallic = builder.Parameter("metallic", Type::Float, { .value = 0.0f });
		const auto metal = builder.Operation(Kind::Multiply, metallic, builder.Texture("metallicRoughnessTexture", 4));
		builder.Link(builder.Operation(Kind::Multiply, metal, builder.Texture("metallicTexture", 2)), output, 2);
		builder.Begin("ラフネス");
		const auto roughness = builder.Parameter("roughness", Type::Float, { .value = 0.5f });
		const auto rough = builder.Operation(Kind::Multiply, roughness, builder.Texture("metallicRoughnessTexture", 3));
		const auto roughSample = builder.Operation(Kind::Multiply, rough, builder.Texture("roughnessTexture", 2));
		builder.Link(builder.Operation(Kind::Maximum, roughSample, builder.Constant(0.04f)), output, 3);
		builder.Begin("AO");
		builder.Link(builder.Texture("occlusionTexture", 2), output, 4);
		builder.Begin("発光");
		const auto emissive = builder.Parameter("emissiveColor", Type::Color, { .value = Color4(0, 0, 0, 0) });
		const auto intensity = builder.Parameter("emissiveIntensity", Type::Float, { .value = 0.0f });
		builder.Link(builder.Operation(Kind::Multiply,
			builder.Operation(Kind::Multiply, emissive, intensity), builder.Texture("emissiveTexture", 1)), output, 5);
		builder.Begin("切り抜き");
		builder.Link(builder.Constant(1.0f), output, 6);
		builder.Link(builder.Parameter("alphaClip", Type::Float, { .value = 0.0f }), output, 7);
		for (const auto& item : material.parameters) {
			Require(builder.consumed.contains(item.first) ||
				std::any_of(builder.consumed.begin(), builder.consumed.end(), [&](const auto& name) {
					return ResolveMaterialParameterSemantic(item.first) != MaterialParameterSemantic::None &&
						ResolveMaterialParameterSemantic(item.first) == ResolveMaterialParameterSemantic(name);
				}), "未対応のMaterialパラメータです: " + item.first);
		}
		return builder.graph;
	}

	// Material値を安定IDと既存の名前解決で対応付ける
	void ApplyValues(ShaderGraphAsset& graph, const MaterialAsset& material) {

		auto expected = ShaderGraphArtifactCache::CreateMaterial(graph, material.shaderGraph);
		for (const auto& record : material.parameters.GetRecords()) {
			const auto& item = record.namedValue;
			bool found = false;
			for (auto& parameter : graph.parameters) {
				const auto* value = material.parameters.Find(MaterialParameterID::FromUUID(parameter.id));
				if (!value) value = material.parameters.FindByName(parameter.name);
				if (!value && parameter.semantic != MaterialParameterSemantic::None) value = material.parameters.Find(parameter.semantic);
				if (value == &item.second) {
					parameter.defaultValue = ConvertValue(*value, parameter.type);
					found = true;
				}
			}
			for (auto& keyword : graph.keywords) {
				const auto* value = material.parameters.Find(MaterialParameterID::FromUUID(keyword.id));
				if (!value) value = material.parameters.FindByName(keyword.name);
				if (value != &item.second || !keyword.runtimeToggle) continue;
				const auto type = keyword.type == ShaderGraphKeywordType::Boolean ? Type::Boolean : Type::Integer;
				ConvertValue(*value, type);
				keyword.defaultIndex = type == Type::Boolean ? std::get<bool>(value->value) : std::get<int32_t>(value->value);
				Require(type == Type::Boolean || keyword.defaultIndex < keyword.entries.size(), "Keywordの値が範囲外です");
				found = true;
			}
			// 生成Materialに含まれる固定の既定値はノードとは別に維持される
			const auto* fixed = expected.parameters.Find(record.id);
			MaterialParameterSet expectedValue, actualValue;
			if (fixed) expectedValue.Set(MaterialParameterID::FromName(item.first), item.first, MaterialParameterSemantic::None, *fixed);
			actualValue.Set(MaterialParameterID::FromName(item.first), item.first, MaterialParameterSemantic::None, item.second);
			Require(found || (fixed && expectedValue.GetContentHash() == actualValue.GetContentHash()),
				"グラフへ対応付けられないMaterialパラメータです: " + item.first);
		}
	}

	// 派生パス以外をコピーして独自処理が失われることを防ぐ
	void ValidateGraphPasses(ShaderGraphAsset& graph, const MaterialAsset& material,
		const ShaderGraphImportResolver& resolver) {

		auto expected = ShaderGraphArtifactCache::CreateMaterial(graph, material.shaderGraph);
		const auto uncompiled = expected;
		const auto artifact = ShaderGraphArtifactCache::DescribeReferences(graph, material.shaderGraph);
		ShaderGraphArtifactCache::ApplyToMaterial(artifact, expected);
		Require(material.passes.size() == expected.passes.size(), "独自の追加パスは取り込めません");
		for (const auto& pass : material.passes) {
			const auto* original = FindPass(uncompiled, pass.passKind);
			if (original && pass.pipeline == original->pipeline && !pass.shaderOverride &&
				pass.preferredVariant == original->preferredVariant) continue;
			const auto* base = FindPass(expected, pass.passKind);
			Require(base && pass.shaderOverride == base->shaderOverride && pass.preferredVariant == base->preferredVariant,
				"グラフ生成後に変更された独自パイプラインは取り込めません");
			if (pass.pipeline == base->pipeline) continue;
			const auto activeKind = material.renderState.surfaceMode == MaterialSurfaceMode::Transparent ?
				MaterialPassKind::Transparent : MaterialPassKind::Draw;
			Require(graph.target == ShaderGraphTarget::Mesh && pass.passKind == activeKind &&
				graph.domain == ShaderGraphDomain::Surface,
				"実効Mesh描画パス以外の独自パイプラインは取り込めません");
			Require(std::none_of(graph.nodes.begin(), graph.nodes.end(), [](const auto& node) {
				return node.kind == Kind::SamplerState || node.kind == Kind::SubGraph || node.kind == Kind::CustomFunction;
			}), "独自パイプラインのサンプラーを既存ノードへ対応付けられません");
			auto pipelinePass = pass;
			pipelinePass.shaderOverride = {};
			const auto sampler = ImportPipeline(graph, pipelinePass,
				activeKind == MaterialPassKind::Transparent ? BuiltinAssets::Pipelines::DefaultMeshTransparent :
				BuiltinAssets::Pipelines::DefaultMesh, resolver);
			const auto samplerID = UUID::New();
			for (const auto& node : graph.nodes) {
				if (node.kind == Kind::TextureSample) {
					graph.links.push_back(ShaderGraphLink{ .id = UUID::New(),
						.outputNode = samplerID, .inputNode = node.id, .inputSlot = 2 });
				}
			}
			graph.nodes.push_back(ShaderGraphNode{ .id = samplerID, .kind = Kind::SamplerState, .sampler = sampler });
		}
	}

	// 参照の欠損を確認し、コンパイル用コピーだけにファイルパスを解決する
	void ResolveReferences(ShaderGraphAsset& graph, const ShaderGraphImportResolver& resolver) {

		nlohmann::json data;
		for (const auto& parameter : graph.parameters) {
			if (parameter.type != Type::Texture2D) continue;
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
bool Engine::ShaderGraphSettingsImporter::Import(const ShaderGraphAsset& destination,
	AssetID source, AssetType sourceType, const ShaderGraphImportResolver& resolver,
	ShaderGraphAsset& output, std::string& error) {

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
				ApplyValues(candidate, material);
			} else {
				Require(material.domain == MaterialDomain::Surface && material.usage == MaterialUsage::Mesh,
					"通常Materialの取り込みは標準Mesh PBRのみ対応しています");
				const bool transparent = material.renderState.surfaceMode == MaterialSurfaceMode::Transparent;
				const auto kind = transparent ? MaterialPassKind::Transparent :
					(material.renderState.surfaceMode == MaterialSurfaceMode::Masked ? MaterialPassKind::Masked : MaterialPassKind::Draw);
				const auto* pass = FindPass(material, kind);
				Require(pass != nullptr, "実効描画パスがありません");
				const auto standardID = transparent ? BuiltinAssets::Pipelines::DefaultMeshTransparent :
					(kind == MaterialPassKind::Masked ? BuiltinAssets::Pipelines::DefaultMeshMasked : BuiltinAssets::Pipelines::DefaultMesh);
				const auto sampler = ImportPipeline(candidate, *pass, standardID, resolver);
				const auto defaults = Read<MaterialAsset>(BuiltinAssets::Materials::DefaultMesh, AssetType::Material, resolver);
				for (const auto& binding : material.passes) {
					if (binding.passKind == kind) continue;
					const auto* base = FindPass(defaults, binding.passKind);
					Require(base && binding.pipeline == base->pipeline && binding.shaderOverride == base->shaderOverride,
						"独自の追加パスは取り込めません");
				}
				const auto state = candidate.renderState;
				candidate = BuildPBR(material, defaults, sampler);
				candidate.renderState = state;
			}
			if (material.renderState.overridesRenderer) {
				Require(material.renderState.surfaceMode != MaterialSurfaceMode::Auto, "表面方式Autoは取り込めません");
				Require(candidate.domain != ShaderGraphDomain::Surface ||
					material.renderState.phase == (IsShaderGraph3DTarget(candidate.target) ?
						(material.renderState.surfaceMode == MaterialSurfaceMode::Transparent ? RenderPhase::Transparent : RenderPhase::Opaque) :
						ResolveMaterialRenderPhase(material.renderState.surfaceMode, material.renderState.phase)),
					"独自の描画フェーズは取り込めません");
				candidate.surfaceMode = material.renderState.surfaceMode == MaterialSurfaceMode::Transparent ?
					ShaderGraphSurfaceMode::Transparent : ShaderGraphSurfaceMode::Opaque;
				candidate.renderState.alphaClipping = material.renderState.surfaceMode == MaterialSurfaceMode::Masked;
				candidate.renderState.blendMode = material.renderState.blendMode;
				candidate.renderState.castShadows = material.renderState.castShadows;
				candidate.renderState.receiveShadows = material.renderState.receiveShadows;
			}
		}
		candidate.name = destination.name;
		auto compileGraph = candidate;
		ResolveReferences(compileGraph, resolver);
		const auto compiled = ShaderGraphCompiler::Compile(compileGraph, "import.surface.hlsli",
			[&](AssetID id, ShaderGraphAsset& graph) {
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
