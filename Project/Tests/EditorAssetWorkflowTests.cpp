#include "EditorAssetWorkflowTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Assets/Project/ProjectAssetDocumentFactory.h>
#include <Engine/Editor/Assets/Project/EditorSceneDefaults.h>
#include <Engine/Editor/Assets/Preview/ModelPreviewUtility.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Textures/TextureDecoder.h>
#include <Engine/Core/World/Scene/Serialization/SceneDocument.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>

// c++
#include <cmath>
#include <cstring>

namespace {

	// 色付きモデルと画像付きモデルのプレビュー設定を確認する
	bool CheckModelPreviewMaterials() {

		using namespace Engine;
		MeshGPUResource mesh{};
		mesh.subMeshes.resize(2);
		mesh.subMeshes[0].baseColor = Color4(0.2f, 0.4f, 0.6f, 0.8f);
		mesh.subMeshes[1].baseColor = Color4(0.5f, 0.25f, 0.75f, 1.0f);
		const AssetID texture = AssetID::New();
		mesh.subMeshes[1].defaultTextureAssets.baseColorTexture = texture;
		mesh.subMeshes[1].defaultTextureAssets.normalTexture = AssetID::New();
		mesh.subMeshes[1].defaultTextureAssets.roughnessTexture = AssetID::New();
		const auto materials = ModelPreviewUtility::BuildMaterials(mesh.subMeshes);
		if (materials.size() != 2) { return false; }
		for (size_t index = 0; index < materials.size(); ++index) {
			const auto& params = materials[index].materialInstance;
			const auto* color = params.Find(MaterialParameterIDs::BaseColor);
			if (!color || std::get<Color4>(color->value) != mesh.subMeshes[index].baseColor ||
				params.Find(MaterialParameterIDs::NormalTexture) || params.Find(MaterialParameterIDs::RoughnessTexture)) {
				return false;
			}
		}
		return !MeshDrawPathCommon::ResolveSubMeshBaseColorTextureAssetID(mesh, materials, 0) &&
			MeshDrawPathCommon::ResolveSubMeshBaseColorTextureAssetID(mesh, materials, 1) == texture &&
			!MeshDrawPathCommon::ResolveSubMeshNormalTextureAssetID(mesh, materials, 1) &&
			!MeshDrawPathCommon::ResolveSubMeshRoughnessTextureAssetID(mesh, materials, 1);
	}

	// BC5法線のZ復元とY規約を実際のDDSで確認する
	bool CheckNormal(DXGI_FORMAT format) {

		using namespace Engine;
		DirectX::ScratchImage input, compressed;
		if (FAILED(input.Initialize2D(DXGI_FORMAT_R32G32B32A32_FLOAT, 4, 4, 1, 1))) { return false; }
		const bool signedFormat = format == DXGI_FORMAT_BC5_SNORM;
		const float value[] = { signedFormat ? 0.25f : 0.625f, signedFormat ? -0.5f : 0.25f, 1.0f, 1.0f };
		for (size_t index = 0; index < 16; ++index) {
			std::memcpy(input.GetPixels() + index * sizeof(value), value, sizeof(value));
		}
		if (FAILED(DirectX::Compress(*input.GetImage(0, 0, 0), format, DirectX::TEX_COMPRESS_DEFAULT, 1.0f, compressed))) {
			return false;
		}
		DirectX::Blob bytes;
		if (FAILED(DirectX::SaveToDDSMemory(compressed.GetImages(), compressed.GetImageCount(), compressed.GetMetadata(),
			DirectX::DDS_FLAGS_NONE, bytes))) { return false; }
		TextureFileRequestDesc request{};
		request.assetPath = "normal.dds";
		request.snapshotBytes = std::make_shared<const std::string>(static_cast<const char*>(bytes.GetBufferPointer()), bytes.GetBufferSize());
		request.normalMap = true;
		request.importSettings.normalConvention = TextureNormalConvention::DirectX;
		request.importSettings.generateMipmaps = false;
		const auto decoded = TextureDecoder::Decode(request);
		if (!decoded.success || decoded.metadata.format != DXGI_FORMAT_R8G8B8A8_UNORM) { return false; }
		const uint8_t* normal = decoded.image.GetPixels();
		if (std::abs(int(normal[0]) - 159) > 4 || std::abs(int(normal[1]) - 64) > 4 || normal[2] < 225) { return false; }
		request.importSettings.normalConvention = TextureNormalConvention::OpenGL;
		const auto flipped = TextureDecoder::Decode(request);
		if (!flipped.success || flipped.image.GetPixels()[1] < 185 || flipped.image.GetPixels()[2] < 225) { return false; }
		// 同じBC5でもData用途の画像は法線変換しない
		request.normalMap = false;
		const auto data = TextureDecoder::Decode(request);
		return data.success && data.metadata.format == format;
	}

	// 表示行とPreviewのホイール所有を通常のImGui frameで確認する
	bool CheckInspectorInput() {

		ImGuiContext* previous = ImGui::GetCurrentContext();
		ImGuiContext* context = ImGui::CreateContext();
		Engine::ScopedCleanup cleanup([=]() noexcept { ImGui::DestroyContext(context); ImGui::SetCurrentContext(previous); });
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = ImVec2(800.0f, 600.0f);
		io.DeltaTime = 1.0f / 60.0f;
		unsigned char* pixels = nullptr;
		int width = 0, height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		float scroll = 0.0f;
		const auto draw = [&](float wheel) {
			io.AddMousePosEvent(100.0f, 45.0f);
			io.AddMouseWheelEvent(0.0f, wheel);
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(400.0f, 220.0f));
			ImGui::Begin("InspectorInput", nullptr, ImGuiWindowFlags_NoTitleBar);
			scroll = ImGui::GetScrollY();
			ImGui::InvisibleButton("##MeshPreviewInput", ImVec2(300.0f, 80.0f));
			ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
			ImGui::TextUnformatted("GameAssets/Models/Preview.fbx");
			ImGui::TextUnformatted("Mesh");
			ImGui::TextUnformatted("0123456789abcdef");
			ImGui::Dummy(ImVec2(200.0f, 500.0f));
			ImGui::End();
			ImGui::Render();
		};
		draw(0.0f);
		draw(0.0f);
		draw(-1.0f);
		draw(-1.0f);
		return scroll == 0.0f;
	}

	// サムネイルだけ縮小し、通常画像のサイズは保つ
	bool CheckThumbnail() {

		using namespace Engine;
		DirectX::ScratchImage input;
		if (FAILED(input.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 128, 64, 1, 1))) { return false; }
		std::memset(input.GetPixels(), 255, input.GetPixelsSize());
		DirectX::Blob bytes;
		if (FAILED(DirectX::SaveToDDSMemory(input.GetImages(), input.GetImageCount(), input.GetMetadata(),
			DirectX::DDS_FLAGS_NONE, bytes))) { return false; }
		TextureFileRequestDesc request{};
		request.assetPath = "thumbnail.dds";
		request.snapshotBytes = std::make_shared<const std::string>(static_cast<const char*>(bytes.GetBufferPointer()), bytes.GetBufferSize());
		request.importSettings.generateMipmaps = false;
		request.previewMaxDimension = 32;
		const auto thumbnail = TextureDecoder::Decode(request);
		request.previewMaxDimension = 0;
		const auto full = TextureDecoder::Decode(request);
		return thumbnail.success && thumbnail.metadata.width == 32 && thumbnail.metadata.height == 16 &&
			full.success && full.metadata.width == 128 && full.metadata.height == 64;
	}
}

bool NEMTests::TestEditorAssetWorkflow() {

	using namespace Engine;
	const auto scene = nlohmann::json::parse(ProjectAssetDocumentFactory::BuildFileContent(ProjectAssetFileKind::Scene, "NewScene"));
	if (!SceneDocument::ValidateSceneFileRoot(scene) || !SceneDocument::ValidateSerializedLocalFileIDs(scene) ||
		scene["Entities"].size() != 2 || scene["Entities"][0]["LocalFileID"] == scene["Entities"][1]["LocalFileID"]) {
		return false;
	}
	const auto camera = scene["Entities"][0]["Components"]["PerspectiveCamera"].get<PerspectiveCameraComponent>();
	if (camera.common.renderPasses != BuiltinAssets::RenderPasses::Default ||
		!scene["Entities"][1]["Components"].contains("DirectionalLight")) { return false; }
	return CheckNormal(DXGI_FORMAT_BC5_UNORM) && CheckNormal(DXGI_FORMAT_BC5_SNORM) &&
		CheckThumbnail() && CheckInspectorInput() && CheckModelPreviewMaterials();
}

// Bistroの実画像を編集せず、法線用途とData用途を読み分ける
bool NEMTests::TestBistroNormalTextures() {

	using namespace Engine;
	TextureFileRequestDesc request{};
	request.assetPath = "GameAssets/Models/Bistro/BistroExterior/_resources/0/MASTER_Bistro_Main_Door_Normal.dds";
	request.importSettings.generateMipmaps = false;
	const auto source = TextureDecoder::Decode(request);
	request.normalMap = true;
	const auto normal = TextureDecoder::Decode(request);
	if (!source.success || source.metadata.format != DXGI_FORMAT_BC5_UNORM || !normal.success ||
		normal.metadata.format != DXGI_FORMAT_R8G8B8A8_UNORM || source.metadata.width != normal.metadata.width ||
		source.metadata.height != normal.metadata.height || source.metadata.mipLevels != normal.metadata.mipLevels) {
		return false;
	}
	const auto* base = normal.image.GetImage(0, 0, 0);
	for (size_t y = 0; y < base->height; ++y) {
		for (size_t x = 0; x < base->width; ++x) {
			if (base->pixels[y * base->rowPitch + x * 4 + 2] < 127) { return false; }
		}
	}
	return true;
}
