#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>

// c++
#include <filesystem>
#include <string_view>

namespace NEMTests {

	//============================================================================
	//	ShaderGraphCompileFixture class
	//	一回の検証で生成したShaderと共通検索先を管理する
	//============================================================================
	class ShaderGraphCompileFixture {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphCompileFixture(std::filesystem::path generatedRoot, std::filesystem::path shaderRoot);

		// Graphから生成した全描画経路を検証用に保存する
		bool WriteGeneratedGraph(const Engine::ShaderGraphAsset& sourceGraph, std::string_view name) const;

		//--------- accessor -----------------------------------------------------

		const std::filesystem::path& GetGeneratedRoot() const { return generatedRoot_; }
		const std::filesystem::path& GetShaderRoot() const { return shaderRoot_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 生成Shaderの一時出力先
		std::filesystem::path generatedRoot_;
		// EngineのShader検索先
		std::filesystem::path shaderRoot_;
	};
} // NEMTests
