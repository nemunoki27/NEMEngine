#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <array>
#include <cstdint>

// json
#include <json.hpp>

namespace Engine {

	inline constexpr uint32_t kMeshImporterVersion = 2;

	enum class MeshLODTransitionMode : uint8_t {

		Immediate,
		Dither,
	};

	// .metaに保存するMesh取り込み設定
	struct MeshImportSettings {

		bool generateAutomaticLODs = false;
		std::array<float, 3> lodTargetErrors = {
			0.03f, 0.1f, 0.2f,
		};
		std::array<AssetID, 3> manualLODMeshes{};
		MeshLODTransitionMode lodTransition =
			MeshLODTransitionMode::Immediate;

		bool operator==(const MeshImportSettings&) const noexcept = default;
	};

	MeshImportSettings ParseMeshImportSettings(
		const nlohmann::json& data);
	nlohmann::json ToJson(const MeshImportSettings& settings);
} // Engine
