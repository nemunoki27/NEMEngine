#include "EditorAssetWorkflowTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Assets/Project/ProjectAssetDocumentFactory.h>
#include <Engine/Editor/Assets/Project/ProjectAssetFileUtility.h>
#include <Engine/Editor/Assets/Project/ProjectAssetPath.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
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
#include <Engine/Core/Rendering/Renderer/Views/RenderViewResolver.h>
#include <Engine/Core/Platform/Input/InputViewMapping.h>

// c++
#include <cmath>
#include <cstring>
#include <fstream>
// imgui
#include <imgui_internal.h>

namespace {

	// 作成した文書とmetaを揃え、同名の内容を上書きしない
	bool CheckToolAssetCreation() {

		using namespace Engine;
		NEMTests::TestDirectory directory("ToolAssetCreation", RuntimePaths::GetGameAssetsRoot());
		AssetDatabase database;
		database.Init();
		const std::string virtualDirectory = RuntimePaths::ToAssetPath(directory.GetPath());
		std::string diagnostic;
		const AssetID clipID = ProjectAssetFileUtility::CreateRegisteredAsset(database, virtualDirectory,
			ProjectAssetFileKind::AnimationClip, "Motion", diagnostic);
		AnimationClipAsset clip;
		if (!clipID || !LoadAnimationClipAsset(database.ResolveFullPath(clipID), clip) || clip.name != "Motion") return false;
		if (ProjectAssetFileUtility::CreateRegisteredAsset(database, virtualDirectory,
			ProjectAssetFileKind::AnimationClip, "Motion", diagnostic) || diagnostic.empty()) return false;
		if (ProjectAssetFileUtility::CreateRegisteredAsset(database, virtualDirectory,
			ProjectAssetFileKind::AnimationClip, "../Invalid", diagnostic)) return false;
		std::ofstream(directory.GetPath() / "Orphan.animclip.json.meta") << "{}";
		if (ProjectAssetFileUtility::CreateRegisteredAsset(database, virtualDirectory,
			ProjectAssetFileKind::AnimationClip, "Orphan", diagnostic) ||
			std::filesystem::exists(directory.GetPath() / "Orphan.animclip.json")) return false;

		// 複合拡張子だけを除き、名前の途中のピリオドを保つ
		return ProjectAssetPath::SplitAssetFileName(Algorithm::PathFromUTF8("火花.v2.effect.json")).first == "火花.v2" &&
			ProjectAssetPath::SplitAssetFileName("Fire.effect").first == "Fire" &&
			ProjectAssetPath::SplitAssetFileName("cube.obj").first == "cube";
	}

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

	// ラベルだけを縮小し、入力欄の文字サイズは維持する
	bool CheckPropertyLabels() {

		ImGuiContext* previous = ImGui::GetCurrentContext();
		ImGuiContext* context = ImGui::CreateContext();
		Engine::ScopedCleanup cleanup([=]() noexcept { ImGui::DestroyContext(context); ImGui::SetCurrentContext(previous); });
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = ImVec2(800.0f, 600.0f);
		io.DeltaTime = 1.0f / 60.0f;
		io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
		io.Fonts->AddFontDefault();
		for (float scale : {1.0f, 1.5f}) {
			for (float width : {80.0f, 168.0f, 220.0f}) {

				ImGui::GetStyle().FontScaleMain = scale;
				ImGui::NewFrame();
				ImGui::SetNextWindowSize(ImVec2(400.0f, 180.0f));
				ImGui::Begin("PropertyLabels", nullptr, ImGuiWindowFlags_NoSavedSettings);
				const float fontSize = ImGui::GetFontSize();
				const bool row = Engine::MyGUI::BeginPropertyRow("Long property label with a complete visible name",
					{ .labelWidth = width });
				if (!row) return false;
				const float labelEnd = ImGui::GetItemRectMax().x;
				const float separator = ImGui::GetCurrentTable()->Columns[0].MaxX;
				const bool valid = labelEnd <= separator - 1.0f && ImGui::GetFontSize() == fontSize;
				ImGui::InputFloat("##Value", &io.DeltaTime);
				Engine::MyGUI::EndPropertyRow();
				ImGui::End();
				ImGui::EndFrame();
				if (!valid) return false;
			}
		}
		return true;
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

bool NEMTests::TestEditorViewportVisibility() {

	using namespace Engine;
	// 画面を隠してもCamera情報とRenderTexture出力を維持する
	ECSWorld world;
	const Entity camera = world.CreateEntity();
	world.AddComponent<TransformComponent>(camera);
	world.AddComponent<PerspectiveCameraComponent>(camera);
	RenderViewRequest request{};
	request.width = 800;
	request.height = 600;
	request.renderDefaultSurfaceThisFrame = false;
	const auto hidden = RenderViewResolver::ResolveGameCameraViews(request, world);
	if (hidden.size() != 1 || !hidden[0].perspective.valid || request.ShouldRender(hidden[0])) return false;
	const auto historyKey = hidden[0].GetHistoryKey();
	request.renderDefaultSurfaceThisFrame = true;
	const auto shown = RenderViewResolver::ResolveGameCameraViews(request, world);
	if (shown.size() != 1 || !request.ShouldRender(shown[0]) || shown[0].GetHistoryKey() != historyKey) return false;
	world.GetComponent<PerspectiveCameraComponent>(camera).common.targetTexture = AssetID::New();
	request.renderDefaultSurfaceThisFrame = false;
	const auto textureViews = RenderViewResolver::ResolveGameCameraViews(request, world);
	if (textureViews.size() != 1 || !textureViews[0].targetTexture || !request.ShouldRender(textureViews[0])) return false;
	request.renderThisFrame = false;
	if (request.ShouldRender(textureViews[0])) return false;

	// 実際のドッキングタブでPanelの可視状態を確認する
	ImGuiContext* previous = ImGui::GetCurrentContext();
	ImGuiContext* gui = ImGui::CreateContext();
	ScopedCleanup cleanup([=]() noexcept { ImGui::DestroyContext(gui); ImGui::SetCurrentContext(previous); });
	auto& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.DisplaySize = ImVec2(800.0f, 600.0f);
	io.DeltaTime = 1.0f / 60.0f;
	unsigned char* pixels = nullptr;
	int width = 0, height = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	bool gameVisible = false, sceneVisible = false;
	const ImGuiID dockID = ImHashStr("VisibilityDock");
	ImGui::NewFrame();
	ImGui::DockBuilderAddNode(dockID, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockID, io.DisplaySize);
	ImGui::DockBuilderDockWindow("VisibilityGame", dockID);
	ImGui::DockBuilderDockWindow("VisibilityScene", dockID);
	ImGui::DockBuilderFinish(dockID);
	ImGui::EndFrame();
	const auto draw = [&](const char* focused) {
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(io.DisplaySize);
		ImGui::Begin("VisibilityHost", nullptr, ImGuiWindowFlags_NoTitleBar);
		ImGui::DockSpace(dockID);
		ImGui::End();
		if (focused) ImGui::SetWindowFocus(focused);
		gameVisible = ImGui::Begin("VisibilityGame");
		ImGui::End();
		sceneVisible = ImGui::Begin("VisibilityScene");
		ImGui::End();
		ImGui::Render();
	};
	draw(nullptr);
	for (int frame = 0; frame < 3; ++frame) draw("VisibilityScene");
	if (!sceneVisible || gameVisible) return false;
	for (int frame = 0; frame < 3; ++frame) draw("VisibilityGame");
	if (!gameVisible || sceneVisible) return false;

	// 入力領域の解除が他のViewへ影響しないことを確認する
	InputViewMapping mapping;
	InputDeviceState input{};
	input.mousePos = Vector2(10.0f, 10.0f);
	mapping.SetViewRect(InputViewArea::Game, Vector2(0.0f, 0.0f), Vector2(100.0f, 100.0f),
		Vector2(200.0f, 200.0f), InputViewCoordinateSpace::Client);
	mapping.SetViewRect(InputViewArea::Scene, Vector2(0.0f, 0.0f), Vector2(100.0f, 100.0f),
		Vector2(100.0f, 100.0f), InputViewCoordinateSpace::Client);
	if (!mapping.IsMouseOnView(InputViewArea::Game, input)) return false;
	mapping.ClearViewRect(InputViewArea::Game);
	mapping.ClearViewRect(InputViewArea::Game);
	if (mapping.HasViewRect(InputViewArea::Game) || mapping.IsMouseOnView(InputViewArea::Game, input) ||
		mapping.GetMousePosInView(InputViewArea::Game, input) || !mapping.IsMouseOnView(InputViewArea::Scene, input)) return false;
	mapping.SetViewRect(InputViewArea::Game, Vector2(0.0f, 0.0f), Vector2(100.0f, 100.0f),
		Vector2(200.0f, 200.0f), InputViewCoordinateSpace::Client);
	return mapping.GetMousePosInView(InputViewArea::Game, input) == Vector2(20.0f, 20.0f);
}

bool NEMTests::TestEditorAssetWorkflow() {

	using namespace Engine;
	if (!CheckToolAssetCreation()) return false;
	const auto scene = nlohmann::json::parse(ProjectAssetDocumentFactory::BuildFileContent(ProjectAssetFileKind::Scene, "NewScene"));
	if (!SceneDocument::ValidateSceneFileRoot(scene) || !SceneDocument::ValidateSerializedLocalFileIDs(scene) ||
		scene["Entities"].size() != 2 || scene["Entities"][0]["LocalFileID"] == scene["Entities"][1]["LocalFileID"]) {
		return false;
	}
	const auto camera = scene["Entities"][0]["Components"]["PerspectiveCamera"].get<PerspectiveCameraComponent>();
	if (camera.common.renderPasses != BuiltinAssets::RenderPasses::Default ||
		!scene["Entities"][1]["Components"].contains("DirectionalLight")) { return false; }
	return CheckNormal(DXGI_FORMAT_BC5_UNORM) && CheckNormal(DXGI_FORMAT_BC5_SNORM) &&
		CheckPropertyLabels() && CheckThumbnail() && CheckInspectorInput() && CheckModelPreviewMaterials();
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
