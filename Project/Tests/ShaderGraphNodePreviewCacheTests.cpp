#include "ShaderGraphNodePreviewCases.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/ShaderGraph/ShaderGraphPreviewFingerprint.h>
#include <Engine/Editor/Tools/Builtin/ShaderGraph/ShaderGraphPreviewDependencies.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAssetReferences.h>

// c++
#include <array>
#include <fstream>
#include <utility>

namespace {

	using namespace Engine;
	using namespace NEMTests;

	// 更新判定へ必要な内部編集値を確認する
	bool TestGraphEdits() {

		auto graph = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::TextureSample).graph;
		graph.nodes.emplace_back(CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::CustomFunction).graph.nodes.front());
		graph.nodes.emplace_back(ShaderGraphNode{.id = Engine::UUID::New(), .kind = ShaderGraphNodeKind::SamplerState});
		graph.keywords.emplace_back(ShaderGraphKeyword{.id = Engine::UUID::New(), .name = "Feature", .defaultIndex = 1});
		graph.parameters.emplace_back(ShaderGraphParameter{
			.id = Engine::UUID::New(), .name = "Value", .defaultValue = {.25f},
		});
		const uint64_t baseline = ShaderGraphNodePreviewUtility::CalculatePreviewHash(graph);
		using Edit = void(*)(ShaderGraphAsset&);
		const std::array edits{
			Edit{[](ShaderGraphAsset& changed) { changed.nodes[1].functionBody += "\n"; }},
			Edit{[](ShaderGraphAsset& changed) { changed.nodes[0].previewMode = ShaderGraphPreviewMode::TwoDimensional; }},
			Edit{[](ShaderGraphAsset& changed) { changed.nodes[0].previewOutputSlot = 5; }},
			Edit{[](ShaderGraphAsset& changed) { changed.nodes[2].sampler.mipLODBias = .5f; }},
			Edit{[](ShaderGraphAsset& changed) { changed.nodes[2].sampler.minLOD = 1; }},
			Edit{[](ShaderGraphAsset& changed) { changed.nodes[2].sampler.maxLOD = 4; }},
			Edit{[](ShaderGraphAsset& changed) { changed.nodes[2].sampler.maxAnisotropy = 16; }},
			Edit{[](ShaderGraphAsset& changed) { changed.nodes[2].sampler.comparisonFunc = D3D12_COMPARISON_FUNC_NEVER; }},
			Edit{[](ShaderGraphAsset& changed) { changed.nodes[2].sampler.borderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE; }},
			Edit{[](ShaderGraphAsset& changed) { changed.keywords.front().defaultIndex = 0; }},
			Edit{[](ShaderGraphAsset& changed) { changed.keywords.front().runtimeToggle = true; }},
			Edit{[](ShaderGraphAsset& changed) { changed.keywords.front().entries = {"A", "B"}; }},
			Edit{[](ShaderGraphAsset& changed) { changed.parameters.front().defaultValue.value = .75f; }},
			Edit{[](ShaderGraphAsset& changed) { changed.nodes[0].value.value = Vector4(.25f, .5f, .75f, .125f); }},
			Edit{[](ShaderGraphAsset& changed) { changed.nodes[0].settings.channelMask = 3; }},
		};
		for (const auto edit : edits) {
			auto changed = graph;
			edit(changed);
			if (ShaderGraphNodePreviewUtility::CalculatePreviewHash(changed) == baseline) return false;
		}
		return true;
	}

	// 定数の更新とSamplerやReflectionの変更を区別する
	bool TestPipelineKeys() {

		auto testCase = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::TextureSample);
		const Engine::UUID parameterID = Engine::UUID::New(), parameterNode = Engine::UUID::New(), samplerNode = Engine::UUID::New();
		testCase.graph.parameters.emplace_back(ShaderGraphParameter{
			.id = parameterID, .name = "Map", .type = ShaderGraphValueType::Texture2D,
			.semantic = MaterialParameterSemantic::BaseColorTexture, .defaultValue = {AssetID{71, 12}},
		});
		testCase.graph.nodes.emplace_back(ShaderGraphNode{
			.id = parameterNode, .kind = ShaderGraphNodeKind::Parameter, .parameterID = parameterID,
		});
		testCase.graph.nodes.emplace_back(ShaderGraphNode{.id = samplerNode, .kind = ShaderGraphNodeKind::SamplerState});
		testCase.graph.links.emplace_back(ShaderGraphLink{
			.id = Engine::UUID::New(), .outputNode = parameterNode, .inputNode = testCase.nodeID,
		});
		testCase.graph.links.emplace_back(ShaderGraphLink{
			.id = Engine::UUID::New(), .outputNode = samplerNode, .inputNode = testCase.nodeID, .inputSlot = 2,
		});
		const auto compiled = ShaderGraphNodePreviewCompiler::Compile(testCase.graph, testCase.nodeID);
		if (!compiled.Succeeded() || compiled.samplers.size() != 1 || compiled.parameters.size() != 1) return false;
		const auto key = BuildShaderGraphNodePreviewPipelineKey(compiled);
		using SamplerEdit = void(*)(PipelineStaticSamplerSettings&);
		const std::array samplerEdits{
			SamplerEdit{[](PipelineStaticSamplerSettings& value) { value.filter = D3D12_FILTER_MIN_MAG_MIP_POINT; }},
			SamplerEdit{[](PipelineStaticSamplerSettings& value) { value.addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; }},
			SamplerEdit{[](PipelineStaticSamplerSettings& value) { value.addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP; }},
			SamplerEdit{[](PipelineStaticSamplerSettings& value) { value.addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP; }},
			SamplerEdit{[](PipelineStaticSamplerSettings& value) { value.borderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE; }},
			SamplerEdit{[](PipelineStaticSamplerSettings& value) { value.comparisonFunc = D3D12_COMPARISON_FUNC_NEVER; }},
			SamplerEdit{[](PipelineStaticSamplerSettings& value) { value.maxAnisotropy = 8; }},
			SamplerEdit{[](PipelineStaticSamplerSettings& value) { value.mipLODBias = .5f; }},
			SamplerEdit{[](PipelineStaticSamplerSettings& value) { value.minLOD = 1; }},
			SamplerEdit{[](PipelineStaticSamplerSettings& value) { value.maxLOD = 4; }},
		};
		for (const auto edit : samplerEdits) {
			auto changed = compiled;
			edit(changed.samplers.front().settings);
			if (BuildShaderGraphNodePreviewPipelineKey(changed) == key) return false;
		}
		using MetadataEdit = void(*)(ShaderParameterMetadata&);
		const std::array metadataEdits{
			MetadataEdit{[](ShaderParameterMetadata& value) { value.shaderName += "Changed"; }},
			MetadataEdit{[](ShaderParameterMetadata& value) { value.displayName += "Changed"; }},
			MetadataEdit{[](ShaderParameterMetadata& value) { ++value.id.value; }},
			MetadataEdit{[](ShaderParameterMetadata& value) { value.semantic = MaterialParameterSemantic::NormalTexture; }},
			MetadataEdit{[](ShaderParameterMetadata& value) { value.isColor = true; }},
			MetadataEdit{[](ShaderParameterMetadata& value) { value.isTexture = false; }},
			MetadataEdit{[](ShaderParameterMetadata& value) { value.exposed = false; }},
		};
		for (const auto edit : metadataEdits) {
			auto changed = compiled;
			edit(changed.parameters.front());
			if (BuildShaderGraphNodePreviewPipelineKey(changed) == key) return false;
		}
		auto constant = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Constant);
		const auto first = ShaderGraphNodePreviewCompiler::Compile(constant.graph, constant.nodeID);
		constant.graph.nodes.front().value.value = Vector4(.9f, .8f, .7f, .6f);
		const auto changed = ShaderGraphNodePreviewCompiler::Compile(constant.graph, constant.nodeID);
		if (BuildShaderGraphNodePreviewPipelineKey(first) != BuildShaderGraphNodePreviewPipelineKey(changed)) return false;
		const auto sphere = ShaderGraphNodePreviewCompiler::Compile(testCase.graph, testCase.nodeID, 0, {.sphere = true});
		const auto rgb = ShaderGraphNodePreviewCompiler::Compile(testCase.graph, testCase.nodeID, 1);
		if (BuildShaderGraphNodePreviewPipelineKey(sphere) == key || BuildShaderGraphNodePreviewPipelineKey(rgb) == key) return false;
		auto function = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::CustomFunction);
		const auto originalFunction = ShaderGraphNodePreviewCompiler::Compile(function.graph, function.nodeID);
		function.graph.nodes.front().functionBody += "\n";
		const auto changedFunction = ShaderGraphNodePreviewCompiler::Compile(function.graph, function.nodeID);
		return BuildShaderGraphNodePreviewPipelineKey(originalFunction) != BuildShaderGraphNodePreviewPipelineKey(changedFunction);
	}

	// 書き込み時刻が変わらない編集でも内容を更新する
	void RewriteFile(const std::filesystem::path& path, const char* contents, bool keepTime = false) {

		const auto time = keepTime ? std::filesystem::last_write_time(path) : std::filesystem::file_time_type{};
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		stream << contents;
		stream.close();
		if (keepTime) std::filesystem::last_write_time(path, time);
	}

	// ファイルの本体と入れ子includeとAsset参照を確認する
	bool TestExternalFiles() {

		TestDirectory directory("ShaderGraphNodePreviewDependencies");
		const auto rootPath = directory.GetPath() / "Root.hlsli";
		const auto includedPath = directory.GetPath() / "Included.hlsli";
		const auto nestedPath = directory.GetPath() / "Nested.hlsli";
		RewriteFile(rootPath, "#include \"Included.hlsli\"\nvoid PreviewIdentity(float4 In, out float4 Out) { Out=In*2; }");
		RewriteFile(includedPath, "#include \"Nested.hlsli\"\n");
		RewriteFile(nestedPath, "static const float kFactor=0.5f;\n");
		auto graph = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::CustomFunction).graph;
		auto& node = graph.nodes.front();
		node.customFunctionSource = ShaderGraphCustomFunctionSource::File;
		node.functionFile = Algorithm::PathToUTF8(rootPath);
		const uint64_t first = ShaderGraphPreviewDependencies::CalculateHash(graph, nullptr);
		RewriteFile(rootPath, "#include \"Included.hlsli\"\nvoid PreviewIdentity(float4 In, out float4 Out) { Out=In*3; }", true);
		const uint64_t rootChanged = ShaderGraphPreviewDependencies::CalculateHash(graph, nullptr);
		if (first == rootChanged) return false;
		RewriteFile(nestedPath, "static const float kFactor=0.6f;\n", true);
		const uint64_t nestedChanged = ShaderGraphPreviewDependencies::CalculateHash(graph, nullptr);
		if (rootChanged == nestedChanged) return false;
		RewriteFile(nestedPath, "#include \"Root.hlsli\"\n");
		const uint64_t cyclic = ShaderGraphPreviewDependencies::CalculateHash(graph, nullptr);
		if (cyclic != ShaderGraphPreviewDependencies::CalculateHash(graph, nullptr)) return false;

		AssetDatabase database;
		database.Init();
		const AssetID fileID = database.ImportOrGet(Algorithm::PathToUTF8(rootPath), AssetType::Text);
		if (!fileID) return false;
		node.functionFileAsset = fileID;
		node.functionFile = "old-stale-file.hlsli";
		const auto resolved = ShaderGraphPreviewDependencies::ResolveFunctions(graph, &database);
		if (std::filesystem::path(resolved.nodes.front().functionFile) != rootPath) return false;
		const uint64_t resolvedHash = ShaderGraphPreviewDependencies::CalculateHash(graph, &database);
		if (resolvedHash != cyclic) return false;

		const auto childPath = directory.GetPath() / "Child.shadergraph.json";
		auto child = CreateDefaultSurfaceShaderGraph("DependencyChild");
		child.nodes.emplace_back(node);
		const Engine::UUID firstParameter = Engine::UUID::New(), secondParameter = Engine::UUID::New();
		child.parameters.emplace_back(ShaderGraphParameter{.id = firstParameter, .name = "First"});
		child.parameters.emplace_back(ShaderGraphParameter{.id = secondParameter, .name = "Second"});
		if (!JsonFile::Save(childPath, ToJson(child))) return false;
		const AssetID childID = database.ImportOrGet(Algorithm::PathToUTF8(childPath), AssetType::ShaderGraph);
		if (!childID) return false;
		auto parent = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::SubGraph).graph;
		parent.nodes.front().subGraph = childID;
		ShaderGraphAsset loaded;
		if (!ShaderGraphPreviewDependencies::LoadGraph(childID, &database, loaded) ||
			loaded.name != child.name || loaded.nodes.back().functionFile == "old-stale-file.hlsli") return false;
		if (!ShaderGraphAssetReferences::RefreshSubGraphs(parent, &database)) return false;
		const auto firstPort = parent.nodes.front().inputPorts.front().id;
		const Engine::UUID constantID = Engine::UUID::New(), linkID = Engine::UUID::New();
		parent.nodes.emplace_back(ShaderGraphNode{.id = constantID, .kind = ShaderGraphNodeKind::Constant});
		parent.links.emplace_back(ShaderGraphLink{
			.id = linkID, .outputNode = constantID, .inputNode = parent.nodes.front().id,
		});
		const uint64_t originalChild = ShaderGraphPreviewDependencies::CalculateHash(parent, &database);
		child.nodes[1].value.value = Color4(.25f, .5f, .75f, 1);
		std::swap(child.parameters[0], child.parameters[1]);
		child.parameters[0].name = "RenamedSecond";
		if (!JsonFile::Save(childPath, ToJson(child)) ||
			ShaderGraphPreviewDependencies::CalculateHash(parent, &database) == originalChild) return false;
		if (!ShaderGraphAssetReferences::RefreshSubGraphs(parent, &database) || parent.links.front().inputSlot != 1 ||
			parent.nodes.front().inputPorts[1].id != firstPort ||
			parent.nodes.front().inputPorts[1].sourceID != firstParameter) return false;
		return true;
	}
}

bool NEMTests::TestShaderGraphNodePreviewCache() {

	return TestGraphEdits() && TestPipelineKeys() && TestExternalFiles();
}
