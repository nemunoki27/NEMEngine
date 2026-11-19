#include "ShaderGraphNodePreviewCases.h"
#include "ShaderGraphNodePreviewGPUFixture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphFrameConstants.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphRuntimeInputSource.h>

// c++
#include <array>
#include <cmath>

using namespace Engine;

namespace {

	// Atlas内の非ゼロViewportでも実Pixel位置からWorld位置を復元する
	bool TestPixelWorldPosition(NEMTests::ShaderGraphNodePreviewGPUFixture& fixture) {

		ShaderGraphNodePreviewCompileOutput output;
		output.valueType = ShaderGraphValueType::Float3;
		output.pixelHLSL = ShaderGraphStageSource::BuildSurfaceInputDeclaration() +
			ShaderGraphStageSource::BuildTimeConstantsDeclaration() +
			"struct Input { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };\n"
			"float4 main(Input input) : SV_TARGET {\n"
			"\tShaderGraphSurfaceInput graphInput = (ShaderGraphSurfaceInput)0;\n" +
			ShaderGraphStageSource::BuildFrameInputAssignments() +
			"\treturn float4(ShaderGraphWorldPositionFromPixel(float4(input.position.xy, 0.25f, 1.0f), graphInput), 1.0f);\n}\n";
		ShaderGraphTimeConstantsGPU frame;
		frame.viewport = {4, 6, 8, 6};
		frame.screenSize = {8, 6, 0, 0};
		frame.clipToWorld = Matrix4x4::MakeScaleMatrix({2, 3, 4}) * Matrix4x4::MakeTranslateMatrix({5, 7, 11});
		const auto image = fixture.Render(output, 0, 0, 0, &frame);
		for (uint32_t y = 0; y < fixture.GetSize(); ++y) {
			for (uint32_t x = 0; x < fixture.GetSize(); ++x) {
				const auto& pixel = image[y * fixture.GetSize() + x];
				if (x < 4 || 11 < x || y < 6 || 11 < y) {
					if (pixel != Vector4(-999, -999, -999, -999)) return false;
					continue;
				}
				const Vector4 expected{((float(x) + .5f - 4) / 8 * 2 - 1) * 2 + 5,
					(1 - (float(y) + .5f - 6) / 6 * 2) * 3 + 7, 12, 1};
				if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y) || !std::isfinite(pixel.z) ||
					.00001f < std::abs(pixel.x - expected.x) || .00001f < std::abs(pixel.y - expected.y) ||
					.00001f < std::abs(pixel.z - expected.z) || pixel.w != 1) return false;
			}
		}
		return true;
	}
}

bool NEMTests::TestShaderGraphNodePreviewValues(ShaderGraphNodePreviewGPUFixture& fixture) {

	if (!TestPixelWorldPosition(fixture)) return false;
	// Materialの公開状態や更新単位に関係なく既定値を使う
	for (const auto scope : {ShaderGraphParameterScope::PerMaterial, ShaderGraphParameterScope::PerInstance,
		ShaderGraphParameterScope::Global}) {
		for (const bool exposed : {false, true}) {
			auto testCase = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Parameter);
			testCase.graph.parameters.front().scope = scope;
			testCase.graph.parameters.front().exposed = exposed;
			if (!ExpectShaderGraphPreview(fixture, testCase, {.2f, .4f, .6f, .75f})) return false;
		}
	}
	struct TypedValue {

		ShaderGraphValueType type;
		MaterialParameterValue value;
		Vector4 expected;
	};
	const std::array values{
		TypedValue{ShaderGraphValueType::Float, {-2.5f}, {-2.5f, -2.5f, -2.5f, 1}},
		TypedValue{ShaderGraphValueType::Float2, {Vector2(-.5f, 2)}, {-.5f, 2, 0, 1}},
		TypedValue{ShaderGraphValueType::Float3, {Vector3(-.5f, 2, .75f)}, {-.5f, 2, .75f, 1}},
		TypedValue{ShaderGraphValueType::Float4, {Vector4(-.5f, 2, .75f, .25f)}, {-.5f, 2, .75f, .25f}},
		TypedValue{ShaderGraphValueType::Color, {Color4(.25f, .5f, .75f, .125f)}, {.25f, .5f, .75f, .125f}},
		TypedValue{ShaderGraphValueType::Boolean, {true}, {1, 1, 1, 1}},
		TypedValue{ShaderGraphValueType::Integer, {int32_t(-3)}, {-3, -3, -3, 1}},
	};
	for (const auto& value : values) {
		auto testCase = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Constant);
		testCase.graph.nodes.front().valueType = value.type;
		testCase.graph.nodes.front().value = value.value;
		if (!ExpectShaderGraphPreview(fixture, testCase, value.expected)) return false;
	}

	auto divide = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Divide);
	ConnectShaderGraphPreviewValue(divide, 0, ShaderGraphValueType::Float4, {Vector4(4, -6, 8, .2f)});
	ConnectShaderGraphPreviewValue(divide, 1, ShaderGraphValueType::Float4, {Vector4(-2, -3, -4, -2)});
	if (!ExpectShaderGraphPreview(fixture, divide, {-2, 2, -2, -.1f})) return false;

	auto remap = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Remap);
	ConnectShaderGraphPreviewValue(remap, 0, ShaderGraphValueType::Float, {.5f});
	ConnectShaderGraphPreviewValue(remap, 1, ShaderGraphValueType::Float2, {Vector2(1, -1)});
	ConnectShaderGraphPreviewValue(remap, 2, ShaderGraphValueType::Float2, {Vector2(0, 2)});
	if (!ExpectShaderGraphPreview(fixture, remap, {.5f, .5f, .5f, 1})) return false;

	auto power = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Power);
	ConnectShaderGraphPreviewValue(power, 0, ShaderGraphValueType::Float, {.25f});
	ConnectShaderGraphPreviewValue(power, 1, ShaderGraphValueType::Float, {-2.0f});
	if (!ExpectShaderGraphPreview(fixture, power, {16, 16, 16, 1})) return false;

	// Float2の不足成分を中間演算へ混ぜない
	auto multiply = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Multiply);
	ConnectShaderGraphPreviewValue(multiply, 0, ShaderGraphValueType::Float2, {Vector2(.3f, .7f)});
	ConnectShaderGraphPreviewValue(multiply, 1, ShaderGraphValueType::Float2, {Vector2(.2f, .5f)});
	if (!ExpectShaderGraphPreview(fixture, multiply, {.06f, .35f, 0, 1})) return false;

	auto split = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Split);
	ConnectShaderGraphPreviewValue(split, 0, ShaderGraphValueType::Float4, {Vector4(-2, .25f, 4, .125f)});
	const std::array splitValues{-2.0f, .25f, 4.0f, .125f};
	for (uint32_t slot = 0; slot < splitValues.size(); ++slot) {
		const float value = splitValues[slot];
		if (!ExpectShaderGraphPreview(fixture, split, {value, value, value, 1}, slot)) return false;
	}
	auto combine = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Combine);
	for (uint32_t slot = 0; slot < splitValues.size(); ++slot) {
		ConnectShaderGraphPreviewValue(combine, slot, ShaderGraphValueType::Float, {splitValues[slot]});
	}
	if (!ExpectShaderGraphPreview(fixture, combine, {-2, .25f, 4, .125f}, 0) ||
		!ExpectShaderGraphPreview(fixture, combine, {-2, .25f, 4, 1}, 1) || !ExpectShaderGraphPreview(fixture, combine, {-2, .25f, 0, 1}, 2)) return false;
	// 同じNodeの複数出力を同じShader内でも区別する
	auto joined = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Combine);
	ShaderGraphNode splitNode{.id = Engine::UUID::New(), .kind = ShaderGraphNodeKind::Split};
	ShaderGraphNode source{.id = Engine::UUID::New(), .kind = ShaderGraphNodeKind::Constant,
		.valueType = ShaderGraphValueType::Float4, .value = {Vector4(-2, .25f, 4, .125f)}};
	joined.graph.nodes.emplace_back(splitNode);
	joined.graph.nodes.emplace_back(source);
	joined.graph.links.emplace_back(ShaderGraphLink{
		.id = Engine::UUID::New(), .outputNode = source.id, .inputNode = splitNode.id,
	});
	for (uint32_t slot = 0; slot < 4; ++slot) {
		joined.graph.links.emplace_back(ShaderGraphLink{
			.id = Engine::UUID::New(), .outputNode = splitNode.id, .outputSlot = slot,
			.inputNode = joined.nodeID, .inputSlot = slot,
		});
	}
	if (!ExpectShaderGraphPreview(fixture, joined, {-2, .25f, 4, .125f})) return false;

	for (const bool predicate : {false, true}) {
		auto branch = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Branch);
		ConnectShaderGraphPreviewValue(branch, 0, ShaderGraphValueType::Boolean, {predicate});
		ConnectShaderGraphPreviewValue(branch, 1, ShaderGraphValueType::Float4, {Vector4(1, 2, 3, .25f)});
		ConnectShaderGraphPreviewValue(branch, 2, ShaderGraphValueType::Float4, {Vector4(4, 5, 6, .75f)});
		if (!ExpectShaderGraphPreview(fixture, branch, predicate ? Vector4(1, 2, 3, .25f) : Vector4(4, 5, 6, .75f))) return false;
	}
	// 再合流する接続を増やしても式を指数的に展開しない
	auto diamond = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Constant);
	diamond.graph.nodes.front().valueType = ShaderGraphValueType::Float;
	diamond.graph.nodes.front().value.value = .125f;
	const auto base = ShaderGraphNodePreviewCompiler::Compile(diamond.graph, diamond.nodeID, 0, {.rawOutput = true});
	for (uint32_t layer = 0; layer < 8; ++layer) {
		const Engine::UUID next = Engine::UUID::New();
		diamond.graph.nodes.emplace_back(ShaderGraphNode{.id = next, .kind = ShaderGraphNodeKind::Add});
		for (uint32_t slot = 0; slot < 2; ++slot) {
			diamond.graph.links.emplace_back(ShaderGraphLink{
				.id = Engine::UUID::New(), .outputNode = diamond.nodeID, .inputNode = next, .inputSlot = slot,
			});
		}
		diamond.nodeID = next;
	}
	const auto combined = ShaderGraphNodePreviewCompiler::Compile(diamond.graph, diamond.nodeID, 0, {.rawOutput = true});
	if (!combined.Succeeded() || base.pixelHLSL.size() + 4000 < combined.pixelHLSL.size() ||
		!ExpectShaderGraphPreview(fixture, diamond, {32, 32, 32, 1})) return false;
	return fixture.CheckMessages();
}
