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
#include <Engine/Editor/UI/Inspectors/Builtin/Render/TextRendererInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/BillboardInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/InvertedHullOutlineInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/ScreenSpaceOutlineInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Light/DirectionalLightInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Light/PointLightInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Light/SpotLightInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Animation/SkinnedAnimationInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Animation/AnimationPlayerInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Audio/AudioSourceInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/CollisionInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/RigidbodyInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Rigidbody2DInspectorDrawer.h>

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
	ComponentEditorDescriptor transformDescriptor = MakeComponentEditorDescriptor<TransformInspectorDrawer>(
		"Transform", "Transform", "Core");
	transformDescriptor.showInComponentMenu = false;
	registry.Register(std::move(transformDescriptor));
	registry.Register(MakeComponentEditorDescriptor<PerspectiveCameraInspectorDrawer>(
		"PerspectiveCamera", "PerspectiveCamera", "Camera"));
	registry.Register(MakeComponentEditorDescriptor<OrthographicCameraInspectorDrawer>(
		"OrthographicCamera", "OrthographicCamera", "Camera"));
	registry.Register(MakeComponentEditorDescriptor<CameraControllerInspectorDrawer>(
		"Camera Controller", "CameraController", "Camera"));

	// Scriptは複数追加可能で専用の追加コマンドを使う
	ComponentEditorDescriptor scriptDescriptor = MakeComponentEditorDescriptor<ScriptInspectorDrawer>(
		"Script", "Script", "Scripting");
	scriptDescriptor.allowMultiple = true;
	scriptDescriptor.addCommandFactory = [](const Entity& entity) {
		return std::make_unique<AddScriptEntryCommand>(entity);
	};
	registry.Register(std::move(scriptDescriptor));

	registry.Register(MakeComponentEditorDescriptor<AudioSourceInspectorDrawer>(
		"Audio Source", "AudioSource", "Audio"));
	registry.Register(MakeComponentEditorDescriptor<CollisionInspectorDrawer>(
		"Collision", "Collision", "Physics"));
	registry.Register(MakeComponentEditorDescriptor<RigidbodyInspectorDrawer>(
		"Rigidbody", "Rigidbody", "Physics"));
	registry.Register(MakeComponentEditorDescriptor<Rigidbody2DInspectorDrawer>(
		"Rigidbody 2D", "Rigidbody2D", "Physics"));

	// MeshRendererはモデルプレビューで使う参照を呼び出し側へ返す
	meshRendererDrawer = static_cast<MeshRendererInspectorDrawer*>(registry.Register(
		MakeComponentEditorDescriptor<MeshRendererInspectorDrawer>("Mesh Renderer", "MeshRenderer", "Rendering")));
	registry.Register(MakeComponentEditorDescriptor<SpriteRendererInspectorDrawer>(
		"Sprite Renderer", "SpriteRenderer", "Rendering"));
	registry.Register(MakeComponentEditorDescriptor<TextRendererInspectorDrawer>(
		"Text Renderer", "TextRenderer", "Rendering"));
	registry.Register(MakeComponentEditorDescriptor<LineRendererInspectorDrawer>(
		"Line Renderer", "LineRenderer", "Rendering"));
	registry.Register(MakeComponentEditorDescriptor<UVTransformInspectorDrawer>(
		"UVTransform", "UVTransform", "Rendering"));
	registry.Register(MakeComponentEditorDescriptor<BillboardInspectorDrawer>(
		"Billboard", "Billboard", "Rendering"));
	registry.Register(MakeComponentEditorDescriptor<InvertedHullOutlineInspectorDrawer>(
		"Inverted Hull Outline", "InvertedHullOutline", "Rendering"));
	registry.Register(MakeComponentEditorDescriptor<ScreenSpaceOutlineInspectorDrawer>(
		"Screen Space Outline", "ScreenSpaceOutline", "Rendering"));
	registry.Register(MakeComponentEditorDescriptor<SkyboxRendererInspectorDrawer>(
		"Skybox Renderer", "SkyboxRenderer", "Rendering"));

	registry.Register(MakeComponentEditorDescriptor<SkinnedAnimationInspectorDrawer>(
		"Skinned Animation", "SkinnedAnimation", "Animation"));
	registry.Register(MakeComponentEditorDescriptor<AnimationPlayerInspectorDrawer>(
		"Animation Player", "AnimationPlayer", "Animation"));

	registry.Register(MakeComponentEditorDescriptor<DirectionalLightInspectorDrawer>(
		"DirectionalLight", "DirectionalLight", "Lighting"));
	registry.Register(MakeComponentEditorDescriptor<PointLightInspectorDrawer>(
		"PointLight", "PointLight", "Lighting"));
	registry.Register(MakeComponentEditorDescriptor<SpotLightInspectorDrawer>(
		"SpotLight", "SpotLight", "Lighting"));
}
