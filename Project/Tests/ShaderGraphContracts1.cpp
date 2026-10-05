#include "TestContracts.h"

//============================================================================
//	include
//============================================================================
#include "TestFixtures.h"
#include "ShaderGraphCompileFixture.h"
#include "ShaderGraphCompileCases.h"
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

namespace NEMTests {

	bool TestShaderGraphCompile() {

		// 全Caseの終了まで一時出力先を保持する
		TestDirectory directory("ShaderGraph");
		const ShaderGraphCompileFixture fixture(directory.GetPath(), Engine::RuntimePaths::GetEngineAssetsRoot() / "Shaders");
		std::error_code ec;
		std::filesystem::create_directories(fixture.GetGeneratedRoot(), ec);
		if (ec || !TestShaderGraphSettingsImport(fixture)) {
			return false;
		}

		Engine::ShaderGraphAsset graph = Engine::CreateDefaultSurfaceShaderGraph("NEMTest");
		// 既存の検証順と最初の失敗を維持する
		return TestShaderGraphDefaultDomains(fixture, graph) && TestShaderGraphRasterTargets(fixture) &&
			   TestShaderGraphShaderDefinitions() && TestShaderGraphStaticSamplers(fixture) &&
			   TestShaderGraphVertexTargets(fixture) && TestShaderGraphNodeDefaults() && TestShaderGraphDither(fixture) &&
			   TestShaderGraphTimeOutputs() && TestShaderGraphSubGraphs(graph);
	}
} // NEMTests
