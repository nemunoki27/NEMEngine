#include "CollisionManagerTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Collision/CollisionSettings.h>
#include <Engine/Core/ECS/Component/Builtin/CollisionComponent.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/Scene/Header/SceneHeader.h>
#include <Engine/MathLib/Matrix4x4.h>
#include <Engine/MathLib/Quaternion.h>
#include <Engine/Utility/ImGui/MyGUI.h>

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
#include <Engine/Core/Graphics/Line/LineRenderer.h>
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

	// Vector3の各要素を絶対値にする
	Engine::Vector3 AbsVector(const Engine::Vector3& value) {

		return Engine::Vector3(std::fabs(value.x), std::fabs(value.y), std::fabs(value.z));
	}

	// 長さが0に近い場合はfallbackを返す
	Engine::Vector3 NormalizeOr(const Engine::Vector3& value, const Engine::Vector3& fallback) {

		const float length = value.Length();
		if (length <= 0.0001f) {
			return fallback;
		}
		return value / length;
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
		renderer->DrawSphere(MakeWorldCenter(shape, transform), radius, color, 8, thickness);
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

	// 形状タイプごとの描画関数へ振り分ける
	void DrawCollisionShape(const Engine::CollisionShape& shape, const Engine::TransformComponent& transform) {

		const Engine::Color4 color = GetShapeColor(shape);
		const float thickness = 2.0f;
		switch (shape.type) {
		case Engine::ColliderShapeType::Circle2D:
			DrawCircle2D(shape, transform, color, thickness);
			break;
		case Engine::ColliderShapeType::Quad2D:
			DrawQuad2D(shape, transform, color, thickness);
			break;
		case Engine::ColliderShapeType::Sphere3D:
			DrawSphere3D(shape, transform, color, thickness);
			break;
		case Engine::ColliderShapeType::AABB3D:
			DrawAABB3D(shape, transform, color, thickness);
			break;
		case Engine::ColliderShapeType::OBB3D:
			DrawOBB3D(shape, transform, color, thickness);
			break;
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

	CollisionSettings& settings = CollisionSettings::GetInstance();
	if (context.toolContext.activeSceneHeader) {
		settings.SetActiveSettingsAssetPath(context.toolContext.activeSceneHeader->collisionSettingsPath);
	}
	settings.EnsureLoaded();

	ImGui::SetWindowFontScale(0.64f);

	// 現在開いているシーンが参照するCollision設定ファイルを表示する
	if (context.toolContext.activeSceneHeader) {
		ImGui::TextDisabled("Settings: %s", context.toolContext.activeSceneHeader->collisionSettingsPath.c_str());
	} else {
		const std::string settingsPath = settings.GetSettingsPath().generic_string();
		ImGui::TextDisabled("Settings: %s", settingsPath.c_str());
	}

	// デバッグ用のCollision描画設定
	ImGui::Checkbox("DrawCollisionWorld", &drawCollisionWorld_);
	ImGui::Separator();
	DrawTypes();
	ImGui::Spacing();
	ImGui::Separator();
	DrawMatrix();

	ImGui::SetWindowFontScale(0.64f);

	ImGui::End();
}

void Engine::CollisionManagerTool::DrawTypes() {

	CollisionSettings& settings = CollisionSettings::GetInstance();

	if (!MyGUI::CollapsingHeader("Collision Types")) {
		return;
	}

	const auto& types = settings.GetTypes();

	// Collisionタイプ名と有効状態を編集する
	for (uint32_t i = 0; i < static_cast<uint32_t>(types.size()); ++i) {

		ImGui::PushID(static_cast<int32_t>(i));
		std::string name = types[i].name;
		if (MyGUI::InputText("Name", name).editFinished) {
			settings.SetTypeName(i, name);
		}

		bool enabled = types[i].enabled;
		if (ImGui::Checkbox("Enabled", &enabled)) {
			settings.SetTypeEnabled(i, enabled);
		}
		ImGui::Separator();
		ImGui::PopID();
	}

	if (ImGui::Button("Add Collision Type", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		settings.AddType("CollisionType" + std::to_string(settings.GetTypeCount()));
	}
	if (settings.GetTypeCount() > 1) {
		if (ImGui::Button("Remove Last Collision Type", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			settings.RemoveLastType();
		}
	}
}

void Engine::CollisionManagerTool::DrawMatrix() {

	CollisionSettings& settings = CollisionSettings::GetInstance();
	const auto& types = settings.GetTypes();
	const uint32_t count = static_cast<uint32_t>(types.size());

	if (!MyGUI::CollapsingHeader("Layer Collision Matrix")) {
		return;
	}
	if (count == 0) {
		ImGui::TextDisabled("Collision type is empty.");
		return;
	}

	const ImGuiTableFlags flags =
		ImGuiTableFlags_Borders |
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_SizingFixedFit |
		ImGuiTableFlags_ScrollX;
	if (!ImGui::BeginTable("##CollisionMatrix", static_cast<int32_t>(count + 1), flags)) {
		return;
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
				settings.Save();
			}
			ImGui::PopID();
		}
	}

	ImGui::EndTable();
}

void Engine::CollisionManagerTool::DrawCollisionWorld(ECSWorld& world) const {

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
				DrawCollisionShape(shape, transform);
			}
		});
#else
	(void)world;
#endif
}
