#include "BuiltinComponentEditorRegistration.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Core/ComponentEditorRegistry.h>

// ビルトインDrawer群
#include <Engine/Editor/UI/Inspectors/Builtin/TransformInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/SplineInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/ParticleFieldInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/UVTransformInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/ScriptInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/CameraInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/CameraControllerInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/SpriteRendererInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/LineRendererInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/DecalProjectorInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/SkyboxRendererInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/MeshRendererInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/PrimitiveRendererInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/ParticleSystemInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/ParticlePaintReceiverInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/FlipbookAnimationInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/TextRendererInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/BillboardInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/InvertedHullOutlineInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/ScreenSpaceOutlineInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Light/LightInspectorDrawers.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Animation/SkinnedAnimationInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Animation/AnimationPlayerInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Audio/AudioSourceInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Audio/AudioListenerInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/CollisionInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/RigidbodyInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Rigidbody2DInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/PhysicsJointInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/UI/CanvasInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/UI/UISelectableInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/UI/UIImageButtonInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/UI/UITextButtonInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/UI/UIProgressInspectorDrawer.h>

// c++
#include <memory>
#include <string_view>
#include <utility>

//============================================================================
//	BuiltinComponentEditorRegistration internal
//============================================================================
namespace {

	// コンポーネント編集情報を生成する
	template <typename TDrawer>
	Engine::ComponentEditorDescriptor MakeComponentEditorDescriptor(std::string_view menuLabel,
		std::string_view typeName, std::string_view category, std::string_view editorSourceFile) {

		Engine::ComponentEditorDescriptor descriptor{};
		descriptor.menuLabel = menuLabel;
		descriptor.typeName = typeName;
		descriptor.category = category;
		descriptor.editorSourceFile = editorSourceFile;
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
		"Transform", "Transform", "Core", "UI/Inspectors/Builtin/TransformInspectorDrawer.cpp");
	transformDescriptor.showInComponentMenu = false;
	registry.Register(std::move(transformDescriptor));
	registry.Register(MakeComponentEditorDescriptor<SplineInspectorDrawer>(
		"Spline", "Spline", "Core", "UI/Inspectors/Builtin/SplineInspectorDrawer.cpp"));
	registry.Register(MakeComponentEditorDescriptor<ForceFieldInspectorDrawer>(
		"Force Field", "ForceField", "Physics", "UI/Inspectors/Builtin/ParticleFieldInspectorDrawer.cpp"));
	registry.Register(MakeComponentEditorDescriptor<VectorFieldInspectorDrawer>(
		"Vector Field", "VectorField", "Physics", "UI/Inspectors/Builtin/ParticleFieldInspectorDrawer.cpp"));
	registry.Register(MakeComponentEditorDescriptor<HeightmapColliderInspectorDrawer>(
		"Heightmap Collider", "HeightmapCollider", "Physics", "UI/Inspectors/Builtin/ParticleFieldInspectorDrawer.cpp"));
	// オーディオ
	{
		registry.Register(MakeComponentEditorDescriptor<AudioSourceInspectorDrawer>(
			"Audio Source", "AudioSource", "Audio", "UI/Inspectors/Builtin/Audio/AudioSourceInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<AudioListenerInspectorDrawer>(
			"Audio Listener", "AudioListener", "Audio", "UI/Inspectors/Builtin/Audio/AudioListenerInspectorDrawer.cpp"));
	}
	// カメラ
	{
		registry.Register(MakeComponentEditorDescriptor<PerspectiveCameraInspectorDrawer>(
			"PerspectiveCamera", "PerspectiveCamera", "Camera", "UI/Inspectors/Builtin/CameraInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<OrthographicCameraInspectorDrawer>(
			"OrthographicCamera", "OrthographicCamera", "Camera", "UI/Inspectors/Builtin/CameraInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<CameraControllerInspectorDrawer>(
			"Camera Controller", "CameraController", "Camera", "UI/Inspectors/Builtin/CameraControllerInspectorDrawer.cpp"));
	}
	// 描画系
	{
		meshRendererDrawer = static_cast<MeshRendererInspectorDrawer*>(registry.Register(MakeComponentEditorDescriptor<
			MeshRendererInspectorDrawer>("Mesh Renderer", "MeshRenderer", "Rendering",
				"UI/Inspectors/Builtin/Render/MeshRendererInspectorDrawer.cpp")));
		registry.Register(MakeComponentEditorDescriptor<SpriteRendererInspectorDrawer>(
			"Sprite Renderer", "SpriteRenderer", "Rendering", "UI/Inspectors/Builtin/Render/SpriteRendererInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<TextRendererInspectorDrawer>(
			"Text Renderer", "TextRenderer", "Rendering", "UI/Inspectors/Builtin/Render/TextRendererInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<SkyboxRendererInspectorDrawer>(
			"Skybox Renderer", "SkyboxRenderer", "Rendering", "UI/Inspectors/Builtin/Render/SkyboxRendererInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<DecalProjectorInspectorDrawer>(
			"DecalProjector", "DecalProjector", "Rendering", "UI/Inspectors/Builtin/Render/DecalProjectorInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<LineRendererInspectorDrawer>(
			"Line Renderer", "LineRenderer", "Rendering", "UI/Inspectors/Builtin/Render/LineRendererInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<PrimitiveRendererInspectorDrawer>(
			"Primitive Renderer", "PrimitiveRenderer", "Rendering",
			"UI/Inspectors/Builtin/Render/PrimitiveRendererInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<ParticleSystemInspectorDrawer>(
			"Particle System", "ParticleSystem", "Rendering", "UI/Inspectors/Builtin/Render/ParticleSystemInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<ParticlePaintReceiverInspectorDrawer>(
			"Particle Paint Receiver", "ParticlePaintReceiver", "Rendering", "UI/Inspectors/Builtin/Render/ParticlePaintReceiverInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<FlipbookAnimationInspectorDrawer>(
			"Flipbook Animation", "FlipbookAnimation", "Rendering",
			"UI/Inspectors/Builtin/Render/FlipbookAnimationInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<UVTransformInspectorDrawer>(
			"UVTransform", "UVTransform", "Rendering", "UI/Inspectors/Builtin/UVTransformInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<BillboardInspectorDrawer>(
			"Billboard", "Billboard", "Rendering", "UI/Inspectors/Builtin/Render/BillboardInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<InvertedHullOutlineInspectorDrawer>(
			"Inverted Hull Outline", "InvertedHullOutline", "Rendering",
			"UI/Inspectors/Builtin/Render/InvertedHullOutlineInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<ScreenSpaceOutlineInspectorDrawer>(
			"Screen Space Outline", "ScreenSpaceOutline", "Rendering",
			"UI/Inspectors/Builtin/Render/ScreenSpaceOutlineInspectorDrawer.cpp"));
	}
	// 衝突
	{
		registry.Register(MakeComponentEditorDescriptor<CollisionInspectorDrawer>(
			"Collision", "Collision", "Physics", "UI/Inspectors/Builtin/CollisionInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<RigidbodyInspectorDrawer>(
			"Rigidbody", "Rigidbody", "Physics", "UI/Inspectors/Builtin/RigidbodyInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<Rigidbody2DInspectorDrawer>(
			"Rigidbody 2D", "Rigidbody2D", "Physics", "UI/Inspectors/Builtin/Rigidbody2DInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<FixedJointInspectorDrawer>(
			"Fixed Joint", "FixedJoint", "Physics", "UI/Inspectors/Builtin/PhysicsJointInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<HingeJointInspectorDrawer>(
			"Hinge Joint", "HingeJoint", "Physics", "UI/Inspectors/Builtin/PhysicsJointInspectorDrawer.cpp"));
	}
	// UI
	{
		registry.Register(MakeComponentEditorDescriptor<CanvasInspectorDrawer>(
			"Canvas", "Canvas", "UI", "UI/Inspectors/Builtin/UI/CanvasInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<UISelectableInspectorDrawer>(
			"UI Selectable", "UISelectable", "UI", "UI/Inspectors/Builtin/UI/UISelectableInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<UIImageButtonInspectorDrawer>(
			"UI Image Button", "UIImageButton", "UI", "UI/Inspectors/Builtin/UI/UIImageButtonInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<UITextButtonInspectorDrawer>(
			"UI Text Button", "UITextButton", "UI", "UI/Inspectors/Builtin/UI/UITextButtonInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<UIProgressInspectorDrawer>(
			"UI Progress", "UIProgress", "UI", "UI/Inspectors/Builtin/UI/UIProgressInspectorDrawer.cpp"));
	}
	// アニメーション系
	{
		registry.Register(MakeComponentEditorDescriptor<SkinnedAnimationInspectorDrawer>(
			"Skinned Animation", "SkinnedAnimation", "Animation",
			"UI/Inspectors/Builtin/Animation/SkinnedAnimationInspectorDrawer.cpp"));
		registry.Register(MakeComponentEditorDescriptor<AnimationPlayerInspectorDrawer>(
			"Animation Player", "AnimationPlayer", "Animation", "UI/Inspectors/Builtin/Animation/AnimationPlayerInspectorDrawer.cpp"));
	}
	// ライト系
	{
		registry.Register(MakeComponentEditorDescriptor<DirectionalLightInspectorDrawer>(
			"DirectionalLight", "DirectionalLight", "Lighting", "UI/Inspectors/Builtin/Light/LightInspectorDrawers.cpp"));
		registry.Register(MakeComponentEditorDescriptor<PointLightInspectorDrawer>(
			"PointLight", "PointLight", "Lighting", "UI/Inspectors/Builtin/Light/LightInspectorDrawers.cpp"));
		registry.Register(MakeComponentEditorDescriptor<RectLightInspectorDrawer>(
			"RectLight", "RectLight", "Lighting", "UI/Inspectors/Builtin/Light/LightInspectorDrawers.cpp"));
		registry.Register(MakeComponentEditorDescriptor<SpotLightInspectorDrawer>(
			"SpotLight", "SpotLight", "Lighting", "UI/Inspectors/Builtin/Light/LightInspectorDrawers.cpp"));
	}
	// スクリプト
	{
		ComponentEditorDescriptor scriptDescriptor = MakeComponentEditorDescriptor<ScriptInspectorDrawer>(
			"Script", ScriptComponent::kTypeName, "Scripting", "UI/Inspectors/Builtin/ScriptInspectorDrawer.cpp");
		scriptDescriptor.allowMultiple = true;
		// ScriptComponentは内部コンテナのため、削除は各C#スクリプトのヘッダーから行う
		scriptDescriptor.showInRemoveMenu = false;
		registry.Register(std::move(scriptDescriptor));
	}
}
