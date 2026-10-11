#include "TestContracts.h"

//============================================================================
//	include
//============================================================================
#include "TestFixtures.h"
#include "ShaderGraphCompileFixture.h"
#include "ShaderGraphCompileCases.h"
#include "TestRunner.h"
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

namespace NEMTests {

	bool TestShaderGraphCompile() {

		// 全Caseの終了まで一時出力先を保持する
		TestDirectory directory("ShaderGraph");
		const ShaderGraphCompileFixture fixture(directory.GetPath(), Engine::RuntimePaths::GetEngineAssetsRoot() / "Shaders");
		std::error_code ec;
		std::filesystem::create_directories(fixture.GetGeneratedRoot(), ec);
		if (ec || !RunTest("ShaderGraphSettingsImport", [&] { return TestShaderGraphSettingsImport(fixture); })) {
			return false;
		}

		Engine::ShaderGraphAsset graph = Engine::CreateDefaultSurfaceShaderGraph("NEMTest");
		// 既存の検証順と最初の失敗を維持する
		return RunTest("ShaderGraphDefaultDomains", [&] { return TestShaderGraphDefaultDomains(fixture, graph); }) &&
			RunTest("ShaderGraphRasterTargets", [&] { return TestShaderGraphRasterTargets(fixture); }) &&
			RunTest("ShaderGraphShaderDefinitions", TestShaderGraphShaderDefinitions) &&
			RunTest("ShaderGraphStaticSamplers", [&] { return TestShaderGraphStaticSamplers(fixture); }) &&
			RunTest("ShaderGraphVertexTargets", [&] { return TestShaderGraphVertexTargets(fixture); }) &&
			RunTest("ShaderGraphNodeDefaults", TestShaderGraphNodeDefaults) &&
			RunTest("ShaderGraphDither", [&] { return TestShaderGraphDither(fixture); }) &&
			RunTest("ShaderGraphTimeOutputs", TestShaderGraphTimeOutputs) &&
			RunTest("ShaderGraphSubGraphs", [&] { return TestShaderGraphSubGraphs(graph); });
	}
} // NEMTests
