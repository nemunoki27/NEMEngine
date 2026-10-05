#include "EditorManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Tools/Registry/ToolRegistry.h>
#include <Engine/Core/Runtime/Context/EngineContext.h>
#include <Engine/Editor/UI/Panels/Builtin/BuiltinEditorPanelRegistration.h>
#include <Engine/Editor/Tools/Builtin/BuiltinEditorTools.h>

// c++
#include <memory>
#include <exception>

// imgui
#include <ImGuizmo.h>

//============================================================================
//	EditorManager classMethods
//============================================================================

void Engine::EditorManager::Init(GraphicsCore& graphicsCore) {

	// すでに初期化されている場合は何もしない
	if (initialized_) {
		return;
	}

	auto& engineContext = graphicsCore.GetContext();
	auto& graphicsPlatform = graphicsCore.GetDXObject();

	// ImGuiの初期化
	imguiManager_.Init(engineContext.GetWinApp()->GetHwnd(), graphicsCore.GetSwapChainDesc().BufferCount,
		graphicsPlatform.GetDevice(), graphicsPlatform.GetCommandQueue()->GetQueue(), &graphicsCore.GetSRVDescriptor(),
		graphicsCore.GetBackBufferRenderTarget().format, DXGI_FORMAT_D24_UNORM_S8_UINT);
	initialized_ = true;

	// ImGuizmoのImGuiコンテキストを設定
	ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());

	// ImGuiのレイアウトはEditorLayoutManagerで管理する
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;

	// レイアウト構築フラグをリセット
	requests_ = {};
	dirtyState_ = {};
	pendingDuplicatePanelID_.clear();
	pendingEditorLayout_.reset();
	requestBuildDefaultDockLayout_ = false;

	// エディタ標準ツールの登録
	RegisterBuiltinEditorTools();
	// シーンビューカメラツールを取得
	sceneViewCameraController_ =
		static_cast<SceneViewCameraController*>(Engine::ToolRegistry::GetInstance().Find("engine.sceneViewCamera"));
	LoadViewportPanelState();

	gameBuildSession_ = std::make_unique<EditorGameBuildSession>();

	// 各パネルの生成と登録
	EditorPanelCreateContext panelCreateContext{graphicsCore.GetTextureUploadService()};
	for (auto& panel : CreateBuiltinEditorPanels(panelCreateContext)) {
		panels_.emplace_back(std::move(panel));
	}

	// 保存済みのレイアウトを適用する
	editorLayoutManager_.Init();
	EditorLayoutSnapshot startupLayout{};
	if (editorLayoutManager_.LoadStartupLayout(startupLayout)) {
		ApplyEditorLayout(startupLayout, graphicsCore);
	}

	// シーンビューのメッシュピック処理の初期化
	meshSubMeshPicker_ = std::make_unique<MeshSubMeshPicker>();
	meshSubMeshPicker_->Init(graphicsCore);
	initializationComplete_ = true;
}

void Engine::EditorManager::Finalize() {

	// Editor終了後の補助描画を切り離す
	ParticleEmitterShapeRegistry::GetInstance().SetDebugDrawFunction(nullptr);
	WinApp::EndProductSizePreview();

	if (!initialized_) {
		return;
	}

	std::exception_ptr failure;
	const auto cleanup = [&](auto&& action) {
		try {
			action();
		} catch (...) {
			if (!failure) {
				failure = std::current_exception();
			}
		}
	};
	// 初期化失敗時は保存済みのレイアウトを変更しない
	if (initializationComplete_) {
		cleanup([&]() { editorLayoutManager_.SaveSession(CaptureEditorLayout()); });
		cleanup([&]() { SaveViewportPanelState(); });
	}

	// Frame内の借用を解除してPanelを先に破棄
	initialized_ = false;
	initializationComplete_ = false;
	currentRenderContext_ = nullptr;
	sceneViewCameraController_ = nullptr;
	requests_.ResetPlay();
	pendingDuplicatePanelID_.clear();
	pendingEditorLayout_.reset();
	requestBuildDefaultDockLayout_ = false;

	panels_.clear();
	gameBuildSession_.reset();
	tagSettings_ = {};
	renderingLayerSettings_ = {};

	if (meshSubMeshPicker_) {
		cleanup([&]() { meshSubMeshPicker_->Finalize(); });
		meshSubMeshPicker_.reset();
	}
	// 保存やPickingの終了失敗時もImGuiを切り離す
	cleanup([&]() { imguiManager_.Finalize(); });
	ImGuizmo::SetImGuiContext(nullptr);
	if (failure) {
		std::rethrow_exception(failure);
	}
}
