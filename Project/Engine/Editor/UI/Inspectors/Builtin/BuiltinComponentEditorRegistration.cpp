#include "BuiltinComponentEditorRegistration.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Core/ComponentEditorRegistry.h>
#include <Engine/Editor/Commands/Components/AddScriptEntryCommand.h>

// ビルトインDrawer群
#include <Engine/Editor/UI/Inspectors/Builtin/TransformInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/UVTransformInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/ScriptInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/CameraInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/CameraControllerInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/SpriteRendererInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/LineRendererInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/SkyboxRendererInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/MeshRendererInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/PrimitiveRendererInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/EffectEmitterInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/FlipbookAnimationInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/TextRendererInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/BillboardInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/InvertedHullOutlineInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/ScreenSpaceOutlineInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Light/LightInspectorDrawers.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Animation/SkinnedAnimationInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Animation/AnimationPlayerInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Audio/AudioSourceInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/CollisionInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/RigidbodyInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Rigidbody2DInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/UI/UIComponentInspectorDrawers.h>

// c++
#include <memory>
#include <utility>

//============================================================================
//	BuiltinComponentEditorRegistration internal
//============================================================================
namespace {

	// コンポーネント編集情報を生成する
	template <typename TDrawer>
	Engine::ComponentEditorDescriptor MakeComponentEditorDescriptor(const char* menuLabel,
		const char* typeName, const char* category) {

		Engine::ComponentEditorDescriptor descriptor{};
		descriptor.menuLabel = menuLabel;
		descriptor.typeName = typeName;
		descriptor.category = category;
		descriptor.drawerFactory = [] { return std::make_unique<TDrawer>(); };
		return descriptor;
	}
}

//============================================================================
//	BuiltinComponentEditorRegistration methods
//============================================================================
void Engine::RegisterBuiltinComponentEditors(ComponentEditorRegistry& registry,
	MeshRendererInspectorDrawer*& meshRendererDrawer) {

	// Transformは描画登録だけ行い追加削除メニューには出さない
	ComponentEditorDescriptor transformDescriptor = MakeComponentEditorDescriptor<TransformInspectorDrawer>("Transform", "Transform", "Core");
	transformDescriptor.showInComponentMenu = false;
	registry.Register(std::move(transformDescriptor));
	// オーディオ
	{
		registry.Register(MakeComponentEditorDescriptor<AudioSourceInspectorDrawer>("Audio Source", "AudioSource", "Audio"));
	}
	// カメラ
	{
		registry.Register(MakeComponentEditorDescriptor<PerspectiveCameraInspectorDrawer>("PerspectiveCamera", "PerspectiveCamera", "Camera"));
		registry.Register(MakeComponentEditorDescriptor<OrthographicCameraInspectorDrawer>("OrthographicCamera", "OrthographicCamera", "Camera"));
		registry.Register(MakeComponentEditorDescriptor<CameraControllerInspectorDrawer>("Camera Controller", "CameraController", "Camera"));
	}
	// 衝突
	{
		registry.Register(MakeComponentEditorDescriptor<CollisionInspectorDrawer>("Collision", "Collision", "Physics"));
		registry.Register(MakeComponentEditorDescriptor<RigidbodyInspectorDrawer>("Rigidbody", "Rigidbody", "Physics"));
		registry.Register(MakeComponentEditorDescriptor<Rigidbody2DInspectorDrawer>("Rigidbody 2D", "Rigidbody2D", "Physics"));
	}
	// 描画系
	{
		meshRendererDrawer = static_cast<MeshRendererInspectorDrawer*>(registry.Register(MakeComponentEditorDescriptor<
			MeshRendererInspectorDrawer>("Mesh Renderer", "MeshRenderer", "Rendering")));
		registry.Register(MakeComponentEditorDescriptor<SpriteRendererInspectorDrawer>("Sprite Renderer", "SpriteRenderer", "Rendering"));
		registry.Register(MakeComponentEditorDescriptor<TextRendererInspectorDrawer>("Text Renderer", "TextRenderer", "Rendering"));
		registry.Register(MakeComponentEditorDescriptor<SkyboxRendererInspectorDrawer>("Skybox Renderer", "SkyboxRenderer", "Rendering"));
		registry.Register(MakeComponentEditorDescriptor<LineRendererInspectorDrawer>("Line Renderer", "LineRenderer", "Rendering"));
		registry.Register(MakeComponentEditorDescriptor<PrimitiveRendererInspectorDrawer>("Primitive Renderer", "PrimitiveRenderer", "Rendering"));
		registry.Register(MakeComponentEditorDescriptor<EffectEmitterInspectorDrawer>("Effect Emitter", "EffectEmitter", "Rendering"));
		registry.Register(MakeComponentEditorDescriptor<FlipbookAnimationInspectorDrawer>("Flipbook Animation", "FlipbookAnimation", "Rendering"));
	}
	// UI
	{
		registry.Register(MakeComponentEditorDescriptor<CanvasInspectorDrawer>("Canvas", "Canvas", "UI"));
		registry.Register(MakeComponentEditorDescriptor<UISelectableInspectorDrawer>("UI Selectable", "UISelectable", "UI"));
		registry.Register(MakeComponentEditorDescriptor<UIImageButtonInspectorDrawer>("UI Image Button", "UIImageButton", "UI"));
		registry.Register(MakeComponentEditorDescriptor<UITextButtonInspectorDrawer>("UI Text Button", "UITextButton", "UI"));
		registry.Register(MakeComponentEditorDescriptor<UIProgressInspectorDrawer>("UI Progress", "UIProgress", "UI"));
	}
	{
		registry.Register(MakeComponentEditorDescriptor<UVTransformInspectorDrawer>("UVTransform", "UVTransform", "Rendering"));
		registry.Register(MakeComponentEditorDescriptor<BillboardInspectorDrawer>("Billboard", "Billboard", "Rendering"));
		registry.Register(MakeComponentEditorDescriptor<InvertedHullOutlineInspectorDrawer>("Inverted Hull Outline", "InvertedHullOutline", "Rendering"));
		registry.Register(MakeComponentEditorDescriptor<ScreenSpaceOutlineInspectorDrawer>("Screen Space Outline", "ScreenSpaceOutline", "Rendering"));
	}
	// アニメーション系
	{
		registry.Register(MakeComponentEditorDescriptor<SkinnedAnimationInspectorDrawer>("Skinned Animation", "SkinnedAnimation", "Animation"));
		registry.Register(MakeComponentEditorDescriptor<AnimationPlayerInspectorDrawer>("Animation Player", "AnimationPlayer", "Animation"));
	}
	// ライト系
	{
		registry.Register(MakeComponentEditorDescriptor<DirectionalLightInspectorDrawer>("DirectionalLight", "DirectionalLight", "Lighting"));
		registry.Register(MakeComponentEditorDescriptor<PointLightInspectorDrawer>("PointLight", "PointLight", "Lighting"));
		registry.Register(MakeComponentEditorDescriptor<RectLightInspectorDrawer>("RectLight", "RectLight", "Lighting"));
		registry.Register(MakeComponentEditorDescriptor<SpotLightInspectorDrawer>("SpotLight", "SpotLight", "Lighting"));
	}
	// スクリプト
	{
		ComponentEditorDescriptor scriptDescriptor = MakeComponentEditorDescriptor<ScriptInspectorDrawer>("Script", "Script", "Scripting");
		scriptDescriptor.allowMultiple = true;
		scriptDescriptor.addCommandFactory = [](const Entity& entity) {return std::make_unique<AddScriptEntryCommand>(entity); };
		registry.Register(std::move(scriptDescriptor));
	}
}
