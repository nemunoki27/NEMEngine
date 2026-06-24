#include "CollisionManagerTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Physics/Collision/CollisionSettings.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#endif

// imgui
#include <imgui.h>

// c++
#include <algorithm>
#include <cmath>
#include <string>

//============================================================================
//	CollisionManagerTool classMethods
//============================================================================
namespace {

	// SceneInstanceManagerから実体のSceneHeaderを取得する、無ければtoolContextの参照へフォールバック
	Engine::SceneHeader* ResolveActiveSceneHeader(const Engine::ToolContext& context) {

		if (context.sceneInstances && context.activeSceneInstanceID) {
			if (Engine::SceneInstance* activeScene = context.sceneInstances->Find(context.activeSceneInstanceID)) {
				return &activeScene->header;
			}
		}
		return const_cast<Engine::SceneHeader*>(context.activeSceneHeader);
	}

	// Vector3の各要素を絶対値にする
	Engine::Vector3 AbsVector(const Engine::Vector3& value) {

		return Engine::Vector3(std::fabs(value.x), std::fabs(value.y), std::fabs(value.z));
	}

	// 行列から指定基底方向の軸を取り出す
	Engine::Vector3 ExtractAxis(const Engine::Matrix4x4& matrix, const Engine::Vector3& basis) {

		return Engine::Vector3::TransferNormal(basis, matrix);
	}

	// 行列から指定基底方向のスケールを取り出す
	float ExtractScale(const Engine::Matrix4x4& matrix, const Engine::Vector3& basis) {

		const float length = ExtractAxis(matrix, basis).Length();
		return length <= 0.0001f ? 1.0f : length;
	}

	// TransformのworldMatrixからワールドスケールを取り出す
	Engine::Vector3 ExtractWorldScale(const Engine::TransformComponent& transform) {

		return Engine::Vector3(
			ExtractScale(transform.worldMatrix, Engine::Vector3(1.0f, 0.0f, 0.0f)),
			ExtractScale(transform.worldMatrix, Engine::Vector3(0.0f, 1.0f, 0.0f)),
			ExtractScale(transform.worldMatrix, Engine::Vector3(0.0f, 0.0f, 1.0f)));
	}

	// 形状のワールド中心を作成する
	Engine::Vector3 MakeWorldCenter(const Engine::CollisionShape& shape, const Engine::TransformComponent& transform) {

		return transform.worldMatrix.GetTranslationValue() +
			Engine::Vector3::TransferNormal(shape.offset, transform.worldMatrix);
	}

	// 形状の2Dワールド中心を作成する
	Engine::Vector2 MakeWorldCenter2D(const Engine::CollisionShape& shape, const Engine::TransformComponent& transform) {

		const Engine::Vector3 center = MakeWorldCenter(shape, transform);
		return Engine::Vector2(center.x, center.y);
	}

	// 形状に適用する回転行列を作成する
	Engine::Matrix4x4 MakeShapeRotationMatrix(const Engine::CollisionShape& shape,
		const Engine::TransformComponent& transform) {

		Engine::Quaternion rotation = Engine::Quaternion::FromEulerDegrees(shape.rotationDegrees);
		if (shape.useTransformRotation) {
			rotation = rotation * transform.localRotation;
		}
		return Engine::Quaternion::MakeRotateMatrix(rotation.Normalize());
	}

	// 形状の2D回転角を取得する
	float MakeShapeRotationDegrees2D(const Engine::CollisionShape& shape,
		const Engine::TransformComponent& transform) {

		Engine::Quaternion rotation = Engine::Quaternion::FromEulerDegrees(shape.rotationDegrees);
		if (shape.useTransformRotation) {
			rotation = rotation * transform.localRotation;
		}
		return Engine::Quaternion::ToEulerDegrees(rotation.Normalize()).z;
	}

	// Triggerは黄色、通常形状はシアンで表示する
	Engine::Color4 GetShapeColor(const Engine::CollisionShape& shape) {

		return shape.isTrigger ? Engine::Color4::Yellow(1.0f) : Engine::Color4::Cyan(1.0f);
	}

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	// Circle2DをXY平面に描画する
	void DrawCircle2D(const Engine::CollisionShape& shape,
		const Engine::TransformComponent& transform, const Engine::Color4& color, float thickness) {

		Engine::LineRenderer2D* renderer = Engine::LineRenderer::GetInstance()->Get2D();
		if (!renderer) {
			return;
		}

		const Engine::Vector3 scale = AbsVector(ExtractWorldScale(transform));
		const float radius = shape.radius * (std::max)(scale.x, scale.y);
		renderer->DrawCircle(MakeWorldCenter2D(shape, transform), radius, color, 16, thickness);
	}

	// Quad2DをXY平面に描画する
	void DrawQuad2D(const Engine::CollisionShape& shape,
		const Engine::TransformComponent& transform, const Engine::Color4& color, float thickness) {

		Engine::LineRenderer2D* renderer = Engine::LineRenderer::GetInstance()->Get2D();
		if (!renderer) {
			return;
		}

		const Engine::Vector3 scale = AbsVector(ExtractWorldScale(transform));
		const Engine::Vector2 center = MakeWorldCenter2D(shape, transform);
		const Engine::Vector2 halfSize(shape.halfSize2D.x * scale.x, shape.halfSize2D.y * scale.y);
		if (shape.rotatedQuad) {
			renderer->DrawRect(center, halfSize * 2.0f,
				MakeShapeRotationDegrees2D(shape, transform), color, thickness);
		} else {
			renderer->DrawRect(center, halfSize * 2.0f, color, thickness);
		}
	}

	// Sphere3Dを描画する
	void DrawSphere3D(const Engine::CollisionShape& shape,
		const Engine::TransformComponent& transform, const Engine::Color4& color, float thickness) {

		Engine::LineRenderer3D* renderer = Engine::LineRenderer::GetInstance()->Get3D();
		if (!renderer) {
			return;
		}

		const Engine::Vector3 scale = AbsVector(ExtractWorldScale(transform));
		const float radius = shape.radius * (std::max)({ scale.x, scale.y, scale.z });
		renderer->DrawSphere(MakeWorldCenter(shape, transform), radius, color, thickness);
	}

	// AABB3Dを描画する
	void DrawAABB3D(const Engine::CollisionShape& shape,
		const Engine::TransformComponent& transform, const Engine::Color4& color, float thickness) {

		Engine::LineRenderer3D* renderer = Engine::LineRenderer::GetInstance()->Get3D();
		if (!renderer) {
			return;
		}

		const Engine::Vector3 scale = AbsVector(ExtractWorldScale(transform));
		const Engine::Vector3 center = MakeWorldCenter(shape, transform);
		const Engine::Vector3 halfExtents = shape.halfExtents3D * scale;
		renderer->DrawAABB(center - halfExtents, center + halfExtents, color, thickness);
	}

	// OBB3Dを描画する
	void DrawOBB3D(const Engine::CollisionShape& shape,
		const Engine::TransformComponent& transform, const Engine::Color4& color, float thickness) {

		Engine::LineRenderer3D* renderer = Engine::LineRenderer::GetInstance()->Get3D();
		if (!renderer) {
			return;
		}

		const Engine::Vector3 scale = AbsVector(ExtractWorldScale(transform));
		const Engine::Vector3 halfExtents = shape.halfExtents3D * scale;
		renderer->DrawOBB(MakeWorldCenter(shape, transform), halfExtents,
			MakeShapeRotationMatrix(shape, transform), color, thickness);
	}

	// 形状タイプごとの描画関数へ振り分ける、collidingなら衝突中として赤で描く
	void DrawCollisionShape(const Engine::CollisionShape& shape,
		const Engine::TransformComponent& transform, bool colliding) {

		// 衝突中は形状種別に関わらず赤、それ以外はTrigger黄/通常シアン
		const Engine::Color4 color = colliding ? Engine::Color4::Red(1.0f) : GetShapeColor(shape);
		const float thickness = 2.0f;
		switch (shape.type) {
		case Engine::ColliderShapeType::Circle2D:
			DrawCircle2D(shape, transform, color, thickness);
			break;
		case Engine::ColliderShapeType::Quad2D:
			DrawQuad2D(shape, transform, color, thickness);
			break;
		case Engine::ColliderShapeType::Sphere3D:
		case Engine::ColliderShapeType::AABB3D:
		case Engine::ColliderShapeType::OBB3D: {

			// 3D形状は不透明メッシュに隠れるよう深度オクルージョン対象バッチへ積む
			Engine::LineRenderer3D* renderer = Engine::LineRenderer::GetInstance()->Get3D();
			if (renderer) {
				renderer->SetOccludedMode(true);
			}
			if (shape.type == Engine::ColliderShapeType::Sphere3D) {
				DrawSphere3D(shape, transform, color, thickness);
			} else if (shape.type == Engine::ColliderShapeType::AABB3D) {
				DrawAABB3D(shape, transform, color, thickness);
			} else {
				DrawOBB3D(shape, transform, color, thickness);
			}
			if (renderer) {
				renderer->SetOccludedMode(false);
			}
			break;
		}
		default:
			break;
		}
	}
#endif
}

void Engine::CollisionManagerTool::Tick(ToolContext& context) {

	if (!drawCollisionWorld_ || !context.world) {
		return;
	}
	DrawCollisionWorld(*context.world);
}

void Engine::CollisionManagerTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::CollisionManagerTool::DrawEditorTool(const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::CollisionManagerTool::DrawWindow(const EditorToolContext& context) {

	if (!ImGui::Begin("CollisionManager", &openWindow_)) {
		ImGui::End();
		return;
	}

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	SceneHeader* header = ResolveActiveSceneHeader(context.toolContext);

	CollisionSettings& settings = CollisionSettings::GetInstance();
	if (header) {
		settings.SetActiveSettingsAsset(header->collisionSettings, assetDatabase);
	}
	settings.EnsureLoaded();

	ImGui::SetWindowFontScale(0.64f);

	// 現在開いているシーンが参照するCollision設定ファイルを表示する
	if (header) {
		std::string displayPath = ToString(header->collisionSettings);
		if (assetDatabase) {
			if (const AssetMeta* meta = assetDatabase->Find(header->collisionSettings)) {
				displayPath = meta->assetPath;
			}
		}
		ImGui::TextDisabled("Settings: %s%s", displayPath.c_str(), dirty_ ? " *" : "");
	} else {
		const std::string settingsPath = settings.GetSettingsPath().generic_string();
		ImGui::TextDisabled("Settings: %s", settingsPath.c_str());
	}

	// Save/Reloadボタン、PostProcessStackと同じ作法
	if (ImGui::Button("Save")) {
		EnsureActiveCollisionSettingsAsset(context);
		settings.Save();
		dirty_ = false;
	}
	ImGui::SameLine();
	if (ImGui::Button("Reload")) {
		settings.Load();
		dirty_ = false;
	}

	// 別シーンのCollision設定ファイルを参照して現在のシーンへ結びつける
	if (header) {
		AssetID picked = header->collisionSettings;
		AssetEditSetting setting{};
		if (MyGUI::AssetReferenceField("読み込み", picked, assetDatabase,
			{ AssetType::CollisionSettings }, setting).valueChanged) {

			header->collisionSettings = picked;
			settings.SetActiveSettingsAsset(picked, assetDatabase);
			settings.Load();
			dirty_ = false;
		}
	}

	ImGui::Separator();

	// デバッグ用のCollision描画設定
	ImGui::Checkbox("DrawCollisionWorld", &drawCollisionWorld_);
	ImGui::Separator();
	if (DrawTypes()) {
		dirty_ = true;
	}
	ImGui::Spacing();
	ImGui::Separator();
	if (DrawMatrix()) {
		dirty_ = true;
	}

	ImGui::SetWindowFontScale(1.0f);

	ImGui::End();
}

bool Engine::CollisionManagerTool::DrawTypes() {

	CollisionSettings& settings = CollisionSettings::GetInstance();
	bool changed = false;

	if (!MyGUI::CollapsingHeader("Collision Types")) {
		return changed;
	}

	const auto& types = settings.GetTypes();

	// Collisionタイプ名と有効状態を編集する
	for (uint32_t i = 0; i < static_cast<uint32_t>(types.size()); ++i) {

		ImGui::PushID(static_cast<int32_t>(i));
		std::string name = types[i].name;
		if (MyGUI::InputText("Name", name).editFinished) {
			settings.SetTypeName(i, name);
			changed = true;
		}

		bool enabled = types[i].enabled;
		if (ImGui::Checkbox("Enabled", &enabled)) {
			settings.SetTypeEnabled(i, enabled);
			changed = true;
		}
		ImGui::Separator();
		ImGui::PopID();
	}

	if (ImGui::Button("Add Collision Type", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		settings.AddType("CollisionType" + std::to_string(settings.GetTypeCount()));
		changed = true;
	}

	// タイプが複数ある時だけ、コンボで選んだタイプを削除できるようにする
	if (settings.GetTypeCount() > 1) {

		// 一覧が縮んで選択が範囲外になっていたら先頭へ戻す
		if (removeTypeIndex_ >= static_cast<int32_t>(types.size())) {
			removeTypeIndex_ = 0;
		}

		// 削除ボタンの幅だけ余白を残してコンボを置く
		const float deleteButtonWidth = 60.0f;
		if (MyGUI::BeginPropertyRow("Remove Type")) {

			const float comboWidth = ImGui::GetContentRegionAvail().x - (deleteButtonWidth + ImGui::GetStyle().ItemSpacing.x);
			ImGui::SetNextItemWidth(comboWidth <= 1.0f ? 1.0f : comboWidth);

			if (ImGui::BeginCombo("##RemoveTarget", types[removeTypeIndex_].name.c_str())) {
				for (uint32_t i = 0; i < static_cast<uint32_t>(types.size()); ++i) {

					const bool selected = (removeTypeIndex_ == static_cast<int32_t>(i));
					if (ImGui::Selectable(types[i].name.c_str(), selected)) {
						removeTypeIndex_ = static_cast<int32_t>(i);
					}
					if (selected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}

			ImGui::SameLine();
			if (ImGui::Button("削除", ImVec2(deleteButtonWidth, 0.0f))) {
				settings.RemoveType(static_cast<uint32_t>(removeTypeIndex_));
				removeTypeIndex_ = 0;
				changed = true;
			}
			MyGUI::EndPropertyRow();
		}
	}
	return changed;
}

bool Engine::CollisionManagerTool::DrawMatrix() {

	CollisionSettings& settings = CollisionSettings::GetInstance();
	const auto& types = settings.GetTypes();
	const uint32_t count = static_cast<uint32_t>(types.size());
	bool changed = false;

	if (!MyGUI::CollapsingHeader("Layer Collision Matrix")) {
		return changed;
	}
	if (count == 0) {
		ImGui::TextDisabled("Collision type is empty.");
		return changed;
	}

	const ImGuiTableFlags flags =
		ImGuiTableFlags_Borders |
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_SizingFixedFit |
		ImGuiTableFlags_ScrollX;
	if (!ImGui::BeginTable("##CollisionMatrix", static_cast<int32_t>(count + 1), flags)) {
		return changed;
	}

	ImGui::TableSetupColumn("Type");
	for (uint32_t i = 0; i < count; ++i) {
		ImGui::TableSetupColumn(types[i].name.c_str());
	}
	ImGui::TableHeadersRow();

	// UnityのLayer Collision Matrixに近い見た目で衝突可否を編集する
	for (uint32_t y = 0; y < count; ++y) {

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(types[y].name.c_str());

		for (uint32_t x = 0; x < count; ++x) {

			ImGui::TableSetColumnIndex(static_cast<int32_t>(x + 1));
			ImGui::PushID(static_cast<int32_t>(y * kMaxCollisionTypes + x));
			bool enabled = settings.IsPairEnabled(y, x);
			if (ImGui::Checkbox("##Pair", &enabled)) {
				settings.SetPairEnabled(y, x, enabled);
				changed = true;
			}
			ImGui::PopID();
		}
	}

	ImGui::EndTable();
	return changed;
}

void Engine::CollisionManagerTool::EnsureActiveCollisionSettingsAsset(const EditorToolContext& context) {

	const ToolContext& toolContext = context.toolContext;
	AssetDatabase* assetDatabase = toolContext.assetDatabase;
	SceneHeader* header = ResolveActiveSceneHeader(toolContext);

	if (!assetDatabase || !header) {
		return;
	}
	// 解決できる参照を既に持っているなら作り直さない、リンク切れ時は貼り直す
	if (header->collisionSettings && assetDatabase->Find(header->collisionSettings)) {
		return;
	}

	// 現在のシーンのassetパスを解決する
	std::string scenePath;
	if (toolContext.sceneInstances && toolContext.activeSceneInstanceID) {
		if (const SceneInstance* instance = toolContext.sceneInstances->Find(toolContext.activeSceneInstanceID)) {
			if (const AssetMeta* meta = assetDatabase->Find(instance->sceneAsset)) {
				scenePath = meta->assetPath;
			}
		}
	}
	if (scenePath.empty()) {
		return;
	}

	// シーンのベース込みの既定パスにフォルダを用意し、現在の設定でファイルを作る
	const std::string defaultPath = MakeDefaultCollisionSettingsPath(scenePath);
	const std::filesystem::path fullPath = assetDatabase->ResolveAssetPath(defaultPath);
	std::error_code ec;
	std::filesystem::create_directories(fullPath.parent_path(), ec);

	CollisionSettings& settings = CollisionSettings::GetInstance();
	settings.SetActiveSettingsPath(fullPath);
	settings.Save();

	// assetとして登録し、シーンheaderへ結びつけてアクティブ設定にする
	header->collisionSettings = assetDatabase->ImportOrGet(defaultPath, AssetType::CollisionSettings);
	settings.SetActiveSettingsAsset(header->collisionSettings, assetDatabase);
}

void Engine::CollisionManagerTool::DrawCollisionWorld([[maybe_unused]] ECSWorld& world) const {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	// World内の有効なCollision形状をすべて描画する
	world.ForEach<CollisionComponent, TransformComponent>([](
		Entity, CollisionComponent& collision, TransformComponent& transform) {

			if (!collision.enabled) {
				return;
			}
			for (const CollisionShape& shape : collision.shapes) {
				if (!shape.enabled) {
					continue;
				}
				DrawCollisionShape(shape, transform, collision.runtimeColliding);
			}
		});
#endif
}
