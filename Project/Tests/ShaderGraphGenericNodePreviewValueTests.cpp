#include "ShaderGraphNodePreviewCases.h"
#include "ShaderGraphNodePreviewGPUFixture.h"

//============================================================================
//	include
//============================================================================

// c++
#include <array>
#include <cmath>
#include <algorithm>
#include <utility>

using namespace Engine;

namespace {

	// 行ごとの描画が元の行列を保っているか確認する
	bool TestMatrixValues(NEMTests::ShaderGraphNodePreviewGPUFixture& fixture) {

		using namespace NEMTests;
		for (const auto kind : {ShaderGraphNodeKind::Matrix2, ShaderGraphNodeKind::Matrix3, ShaderGraphNodeKind::Matrix4}) {
			auto matrix = CreateShaderGraphNodePreviewCase(kind);
			const uint32_t width = kind == ShaderGraphNodeKind::Matrix2 ? 2u : kind == ShaderGraphNodeKind::Matrix3 ? 3u : 4u;
			const auto output = ShaderGraphNodePreviewCompiler::Compile(matrix.graph, matrix.nodeID, 0, {.rawOutput = true});
			const auto image = fixture.Render(output);
			for (uint32_t y = 0; y < fixture.GetSize(); ++y) {
				const uint32_t row = std::min(uint32_t((float(y) + .5f) / fixture.GetSize() * width), width - 1);
				const Vector4 expected{row == 0 ? 1.0f : 0.0f, row == 1 ? 1.0f : 0.0f,
					row == 2 ? 1.0f : 0.0f, width < 4 || row == 3 ? 1.0f : 0.0f};
				for (uint32_t x = 0; x < fixture.GetSize(); ++x) {
					const auto& pixel = image[y * fixture.GetSize() + x];
					if (.00001f < std::abs(pixel.x - expected.x) || .00001f < std::abs(pixel.y - expected.y) ||
						.00001f < std::abs(pixel.z - expected.z) || .00001f < std::abs(pixel.w - expected.w)) return false;
				}
			}
		}
		auto determinant = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::MatrixDeterminant);
		ShaderGraphNode source{.id = Engine::UUID::New(), .kind = ShaderGraphNodeKind::Matrix4};
		source.settings.matrixRows = {Vector4{2, 0, 0, 0}, Vector4{0, 3, 0, 0}, Vector4{0, 0, 4, 0}, Vector4{0, 0, 0, 5}};
		determinant.graph.nodes.emplace_back(source);
		determinant.graph.links.emplace_back(ShaderGraphLink{
			.id = Engine::UUID::New(), .outputNode = source.id, .inputNode = determinant.nodeID,
		});
		if (!ExpectShaderGraphPreview(fixture, determinant, {120, 120, 120, 1})) return false;
		for (const bool vectorFirst : {false, true}) {
			auto product = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Multiply);
			ShaderGraphNode matrix{.id = Engine::UUID::New(), .kind = ShaderGraphNodeKind::Matrix2};
			matrix.settings.matrixRows[0] = {2, 1, 0, 0};
			matrix.settings.matrixRows[1] = {3, 4, 0, 0};
			product.graph.nodes.emplace_back(matrix);
			product.graph.links.emplace_back(ShaderGraphLink{
				.id = Engine::UUID::New(), .outputNode = matrix.id, .inputNode = product.nodeID,
				.inputSlot = vectorFirst ? 1u : 0u,
			});
			ConnectShaderGraphPreviewValue(product, vectorFirst ? 0u : 1u, ShaderGraphValueType::Float2, {Vector2(5, 6)});
			if (!ExpectShaderGraphPreview(fixture, product, vectorFirst ? Vector4(28, 29, 0, 1) : Vector4(16, 39, 0, 1))) return false;
		}
		for (const bool matrixProduct : {false, true}) {
			auto product = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Multiply);
			ShaderGraphNode matrix{.id = Engine::UUID::New(), .kind = ShaderGraphNodeKind::Matrix2};
			matrix.settings.matrixRows[0] = {2, 1, 0, 0};
			matrix.settings.matrixRows[1] = {3, 4, 0, 0};
			product.graph.nodes.emplace_back(matrix);
			product.graph.links.emplace_back(ShaderGraphLink{
				.id = Engine::UUID::New(), .outputNode = matrix.id, .inputNode = product.nodeID,
			});
			if (matrixProduct) {
				product.graph.links.emplace_back(ShaderGraphLink{
					.id = Engine::UUID::New(), .outputNode = matrix.id, .inputNode = product.nodeID, .inputSlot = 1,
				});
			} else {
				ConnectShaderGraphPreviewValue(product, 1, ShaderGraphValueType::Float, {2.0f});
			}
			const Engine::UUID splitID = Engine::UUID::New();
			product.graph.nodes.emplace_back(ShaderGraphNode{.id = splitID, .kind = ShaderGraphNodeKind::MatrixSplit});
			product.graph.links.emplace_back(ShaderGraphLink{
				.id = Engine::UUID::New(), .outputNode = product.nodeID, .inputNode = splitID,
			});
			product.nodeID = splitID;
			if (!ExpectShaderGraphPreview(fixture, product, matrixProduct ? Vector4(7, 6, 0, 0) : Vector4(4, 2, 0, 0), 0) ||
				!ExpectShaderGraphPreview(fixture, product, matrixProduct ? Vector4(18, 19, 0, 0) : Vector4(6, 8, 0, 0), 1)) return false;
		}
		return true;
	}

	// 色とAlphaの補間を別々のキーで確認する
	bool TestGradientValues(NEMTests::ShaderGraphNodePreviewGPUFixture& fixture) {

		using namespace NEMTests;
		auto gradient = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::SampleGradient);
		ConnectShaderGraphPreviewValue(gradient, 1, ShaderGraphValueType::Float, {.25f});
		ShaderGraphNode source{.id = Engine::UUID::New(), .kind = ShaderGraphNodeKind::Gradient};
		source.settings.gradientColors = {{1, 0, 0, 0}, {0, 1, 0, .5f}, {0, 0, 1, 1}};
		source.settings.gradientAlphas = {{.2f, 0}, {.6f, .5f}, {1, 1}};
		gradient.graph.nodes.emplace_back(source);
		gradient.graph.links.emplace_back(ShaderGraphLink{
			.id = Engine::UUID::New(), .outputNode = source.id, .inputNode = gradient.nodeID,
		});
		const float halfLinear = std::pow((.5f + .055f) / 1.055f, 2.4f);
		if (!ExpectShaderGraphPreview(fixture, gradient, {halfLinear, halfLinear, 0, .4f})) return false;
		gradient.graph.nodes.back().settings.gradientMode = ShaderGraphGradientMode::Fixed;
		return ExpectShaderGraphPreview(fixture, gradient, {0, 1, 0, .6f});
	}

	// 反射率のPresetと屈折率からの計算を確認する
	bool TestReflectanceValues(NEMTests::ShaderGraphNodePreviewGPUFixture& fixture) {

		using namespace NEMTests;
		for (const auto& [mode, expected] : std::array{
			std::pair{ShaderGraphDielectricMaterial::Common, .041f},
			std::pair{ShaderGraphDielectricMaterial::RustedMetal, .030f},
			std::pair{ShaderGraphDielectricMaterial::Water, .020f},
			std::pair{ShaderGraphDielectricMaterial::Ice, .018f},
			std::pair{ShaderGraphDielectricMaterial::Glass, .040f}}) {
			auto testCase = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::DielectricSpecular);
			testCase.graph.nodes.front().settings.dielectricMaterial = mode;
			if (!ExpectShaderGraphPreview(fixture, testCase, {expected, expected, expected, 1})) return false;
		}
		for (const float ior : {1.0f, 1.5f, 2.0f}) {
			auto testCase = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::DielectricSpecular);
			testCase.graph.nodes.front().settings.dielectricMaterial = ShaderGraphDielectricMaterial::Custom;
			ConnectShaderGraphPreviewValue(testCase, 1, ShaderGraphValueType::Float, {ior});
			const float ratio = (ior - 1) / (ior + 1), expected = ratio * ratio;
			if (!ExpectShaderGraphPreview(fixture, testCase, {expected, expected, expected, 1})) return false;
		}
		const std::array reflectance{
			Vector4(.560f, .570f, .580f, 1), Vector4(.972f, .960f, .915f, 1),
			Vector4(.913f, .921f, .925f, 1), Vector4(1, .766f, .336f, 1),
			Vector4(.955f, .637f, .538f, 1), Vector4(.550f, .556f, .554f, 1),
			Vector4(.660f, .609f, .526f, 1), Vector4(.542f, .497f, .449f, 1),
			Vector4(.662f, .655f, .634f, 1), Vector4(.672f, .637f, .585f, 1),
		};
		for (uint32_t index = 0; index < reflectance.size(); ++index) {
			auto testCase = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::MetalReflectance);
			testCase.graph.nodes.front().settings.metalMaterial = static_cast<ShaderGraphMetalMaterial>(index);
			if (!ExpectShaderGraphPreview(fixture, testCase, reflectance[index])) return false;
		}
		return true;
	}
}

bool NEMTests::TestShaderGraphGenericNodePreviewValues(ShaderGraphNodePreviewGPUFixture& fixture) {

	struct UnaryCase {

		ShaderGraphNodeKind kind;
		float input;
		float expected;
	};
	const std::array cases{
		UnaryCase{ShaderGraphNodeKind::Exponential, .5f, std::exp(.5f)},
		UnaryCase{ShaderGraphNodeKind::Log, .5f, std::log(.5f)},
		UnaryCase{ShaderGraphNodeKind::Reciprocal, .25f, 4},
		UnaryCase{ShaderGraphNodeKind::ReciprocalSquareRoot, .25f, 2},
		UnaryCase{ShaderGraphNodeKind::Ceiling, .25f, 1},
		UnaryCase{ShaderGraphNodeKind::Round, .75f, 1},
		UnaryCase{ShaderGraphNodeKind::Sign, -.75f, -1},
		UnaryCase{ShaderGraphNodeKind::Truncate, -.75f, 0},
		UnaryCase{ShaderGraphNodeKind::Arccosine, .5f, std::acos(.5f)},
		UnaryCase{ShaderGraphNodeKind::Arcsine, .5f, std::asin(.5f)},
		UnaryCase{ShaderGraphNodeKind::Arctangent, .5f, std::atan(.5f)},
		UnaryCase{ShaderGraphNodeKind::HyperbolicCosine, .5f, std::cosh(.5f)},
		UnaryCase{ShaderGraphNodeKind::HyperbolicSine, .5f, std::sinh(.5f)},
		UnaryCase{ShaderGraphNodeKind::HyperbolicTangent, .5f, std::tanh(.5f)},
		UnaryCase{ShaderGraphNodeKind::DegreesToRadians, 30, .5235987756f},
		UnaryCase{ShaderGraphNodeKind::RadiansToDegrees, .5235987756f, 30},
		UnaryCase{ShaderGraphNodeKind::Tangent, .5f, std::tan(.5f)},
		UnaryCase{ShaderGraphNodeKind::SquareWave, .25f, 1},
		UnaryCase{ShaderGraphNodeKind::SawtoothWave, .25f, .5f},
		UnaryCase{ShaderGraphNodeKind::TriangleWave, .25f, 0},
	};
	for (const auto& source : cases) {
		auto testCase = CreateShaderGraphNodePreviewCase(source.kind);
		ConnectShaderGraphPreviewValue(testCase, 0, ShaderGraphValueType::Float, {source.input});
		if (!ExpectShaderGraphPreview(fixture, testCase, {source.expected, source.expected, source.expected, 1})) return false;
	}
	for (const auto kind : {ShaderGraphNodeKind::Modulo, ShaderGraphNodeKind::Posterize}) {
		auto testCase = CreateShaderGraphNodePreviewCase(kind);
		ConnectShaderGraphPreviewValue(testCase, 0, ShaderGraphValueType::Float, {.75f});
		ConnectShaderGraphPreviewValue(testCase, 1, ShaderGraphValueType::Float,
			{kind == ShaderGraphNodeKind::Modulo ? .5f : 2.0f});
		const float expected = kind == ShaderGraphNodeKind::Modulo ? .25f : .5f;
		if (!ExpectShaderGraphPreview(fixture, testCase, {expected, expected, expected, 1})) return false;
	}
	for (const auto kind : {ShaderGraphNodeKind::Projection, ShaderGraphNodeKind::Rejection}) {
		auto testCase = CreateShaderGraphNodePreviewCase(kind);
		ConnectShaderGraphPreviewValue(testCase, 0, ShaderGraphValueType::Float3, {Vector3(1, 2, 3)});
		ConnectShaderGraphPreviewValue(testCase, 1, ShaderGraphValueType::Float3, {Vector3(1, 0, 0)});
		if (!ExpectShaderGraphPreview(fixture, testCase,
			kind == ShaderGraphNodeKind::Projection ? Vector4(1, 0, 0, 1) : Vector4(0, 2, 3, 1))) return false;
	}
	for (const auto kind : {ShaderGraphNodeKind::All, ShaderGraphNodeKind::Any}) {
		auto testCase = CreateShaderGraphNodePreviewCase(kind);
		ConnectShaderGraphPreviewValue(testCase, 0, ShaderGraphValueType::Float3, {Vector3(0, 1, 0)});
		const float expected = kind == ShaderGraphNodeKind::Any ? 1.0f : 0.0f;
		if (!ExpectShaderGraphPreview(fixture, testCase, {expected, expected, expected, 1})) return false;
	}
	auto swizzle = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Swizzle);
	swizzle.graph.nodes.front().settings.swizzle = {1, 2, 0, 0};
	ConnectShaderGraphPreviewValue(swizzle, 0, ShaderGraphValueType::Float4, {Vector4(.25f, .5f, .75f, .9f)});
	if (!ExpectShaderGraphPreview(fixture, swizzle, {.5f, .75f, .25f, .25f})) return false;
	auto mask = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::ChannelMask);
	mask.graph.nodes.front().settings.channelMask = 5;
	ConnectShaderGraphPreviewValue(mask, 0, ShaderGraphValueType::Float4, {Vector4(.25f, .5f, .75f, .9f)});
	if (!ExpectShaderGraphPreview(fixture, mask, {.25f, 0, .75f, 0})) return false;
	auto convert = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::ColorspaceConversion);
	ConnectShaderGraphPreviewValue(convert, 0, ShaderGraphValueType::Float3, {Vector3(1, 0, 0)});
	if (!ExpectShaderGraphPreview(fixture, convert, {0, 1, 1, 1})) return false;
	auto contrast = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Contrast);
	contrast.graph.nodes.front().inputPorts.resize(2);
	contrast.graph.nodes.front().inputPorts[0].hasDefault = false;
	contrast.graph.nodes.front().inputPorts[1].type = ShaderGraphValueType::Float;
	contrast.graph.nodes.front().inputPorts[1].defaultValue.value = 2.0f;
	ConnectShaderGraphPreviewValue(contrast, 0, ShaderGraphValueType::Float3, {Vector3(.25f, .5f, .75f)});
	if (!ExpectShaderGraphPreview(fixture, contrast, {.5f - .21763764f, 1.0f - .21763764f, 1.5f - .21763764f, 1})) return false;
	auto gradient = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::SampleGradient);
	ConnectShaderGraphPreviewValue(gradient, 1, ShaderGraphValueType::Float, {.25f});
	const float quarterLinear = std::pow((.25f + .055f) / 1.055f, 2.4f);
	if (!ExpectShaderGraphPreview(fixture, gradient, {quarterLinear, quarterLinear, quarterLinear, 1})) return false;
	return TestMatrixValues(fixture) && TestGradientValues(fixture) && TestReflectanceValues(fixture) && fixture.CheckMessages();
}
