#include "TestContracts.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Materials/MaterialParameterHash.h>
#include <Engine/Editor/Tools/Builtin/ShaderGraph/ShaderGraphPreviewFingerprint.h>

// c++
#include <array>

namespace {

	// 登録と削除の後で内容Hashを更新する
	bool TestMaterialParameterMutation() {

		using namespace Engine;
		MaterialParameterSet parameters;
		MaterialParameterValue first{.value = 1.25f}, second{.value = 2.0f};
		parameters.Set("first", first);
		uint64_t firstHash = parameters.GetContentHash(), revision = parameters.GetRevision();
		if (parameters.try_emplace("first", second).second || parameters.GetContentHash() != firstHash ||
			parameters.GetRevision() != revision) return false;
		if (!parameters.emplace("second", second).second || parameters.GetContentHash() == firstHash ||
			parameters.GetRevision() != revision + 1) return false;
		uint64_t bothHash = parameters.GetContentHash();
		revision = parameters.GetRevision();
		if (parameters.erase("first") != 1 || parameters.GetContentHash() == bothHash ||
			parameters.GetRevision() != revision + 1 || parameters.Find(MaterialParameterID::FromName("first"))) return false;
		auto next = parameters.erase(parameters.begin());
		if (next != parameters.end() || !parameters.empty() ||
			parameters.GetContentHash() != 0 || parameters.GetRevision() != 0) return false;
		// 空になった集合も再登録できる
		parameters.Set("first", first);
		if (parameters.GetContentHash() != firstHash) return false;
		revision = parameters.GetRevision();
		parameters.Set(MaterialParameterID::FromName("first"), "first", MaterialParameterSemantic::BaseColor, second);
		if (parameters.GetRevision() != revision + 1 || parameters.GetContentHash() == firstHash ||
			!parameters.Find(MaterialParameterSemantic::BaseColor)) return false;
		if (parameters.erase(MaterialParameterID::FromName("first")) != 1 || !parameters.empty() ||
			parameters.GetContentHash() != 0 || parameters.GetRevision() != 0) return false;
		if (parameters.erase(MaterialParameterID::FromName("first")) != 0 || parameters.GetRevision() != 0) return false;
		parameters.Set("first", first);
		parameters.clear();
		return parameters.empty() && parameters.GetContentHash() == 0 && parameters.GetRevision() == 0;
	}
}

bool NEMTests::TestMaterialParameterHash() {

	using namespace Engine;
	// 分離前の9種類のHash値を維持する
	const std::array<MaterialParameterValue, 9> values{{
		{.value = 1.25f},
		{.value = Vector2{1.0f, -2.0f}},
		{.value = Vector3{1.0f, -2.0f, 3.0f}},
		{.value = Vector4{1.0f, -2.0f, 3.0f, 4.0f}},
		{.value = Color4{1.0f, 0.0f, 0.5f, 1.0f}},
		{.value = AssetID{1, 2}},
		{.value = int32_t{-7}},
		{.value = UINT32_MAX},
		{.value = true},
	}};
	const std::array<uint64_t, 9> expected{{
		0x9e3779b9beea7c15ull,
		0xcd94bf21df564c7eull,
		0xfb58cedbd3b91a1dull,
		0x4819aabe77bf6695ull,
		0x4818ff20bb51e124ull,
		0xcd94bf3ecef612b9ull,
		0x9e3779b97f4a7d89ull,
		0x9e3779ba7f4a7dd2ull,
		0x9e3779b97f4a7e10ull,
	}};
	for (size_t index = 0; index < values.size(); ++index) {

		if (MaterialParameterHash::HashValue(values[index]) != expected[index] ||
			ShaderGraphNodePreviewUtility::HashPreviewValue(values[index]) != expected[index]) {
			return false;
		}
	}
	return TestMaterialParameterMutation();
}
