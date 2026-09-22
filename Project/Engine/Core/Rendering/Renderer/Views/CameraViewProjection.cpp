#include "CameraViewProjection.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/Foundation/Math/Math.h>

namespace Engine::CameraViewProjection {

	Engine::Matrix4x4 BuildManualWorld(const Engine::ManualRenderCameraTransform& transform) {

		return Engine::Matrix4x4::MakeAffineMatrix(Engine::Vector3::AnyInit(1.0f), transform.rotation, transform.pos);
	}

	Engine::ResolvedCameraView BuildScreenCamera(uint32_t width, uint32_t height) {

		Engine::ResolvedCameraView out{};
		out.valid = 0 < width && 0 < height;
		out.usesManualCamera = true;
		out.nearClip = -1000.0f;
		out.farClip = 1000.0f;
		out.matrices.viewMatrix = Engine::Matrix4x4::Identity();
		out.matrices.inverseViewMatrix = Engine::Matrix4x4::Identity();
		out.matrices.projectionMatrix = Engine::Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f,
			static_cast<float>(width), static_cast<float>(height), out.nearClip, out.farClip);
		out.matrices.inverseProjectionMatrix = Engine::Matrix4x4::Inverse(out.matrices.projectionMatrix);
		out.matrices.viewProjectionMatrix = out.matrices.projectionMatrix;
		return out;
	}

	void UpdateOrthographicCameraMatrices(const Engine::TransformComponent& transform,
		Engine::OrthographicCameraComponent& camera, uint32_t width, uint32_t height) {

		camera.common.aspectRatio = static_cast<float>(width) / static_cast<float>((std::max)(height, 1u));
		camera.common.viewMatrix = Engine::Matrix4x4::Inverse(transform.worldMatrix);
		// 深度範囲を前後対称にし、カメラと同じz平面のSpriteがニアクリップ境界で消えないようにする
		const float orthoDepthRange = (std::max)(camera.farClip, 1.0f);
		camera.common.projectionMatrix = Engine::Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f,
			static_cast<float>(width), static_cast<float>(height), -orthoDepthRange, orthoDepthRange);
		camera.common.viewProjectionMatrix = camera.common.viewMatrix * camera.common.projectionMatrix;
	}

	void UpdatePerspectiveCameraMatrices(const Engine::TransformComponent& transform,
		Engine::PerspectiveCameraComponent& camera, uint32_t width, uint32_t height) {

		camera.common.aspectRatio = static_cast<float>(width) / static_cast<float>((std::max)(height, 1u));
		camera.common.viewMatrix = Engine::Matrix4x4::Inverse(transform.worldMatrix);
		camera.common.projectionMatrix = Engine::Matrix4x4::MakePerspectiveFovMatrix(
			camera.fovY, camera.common.aspectRatio, camera.nearClip, camera.farClip);
		camera.common.viewProjectionMatrix = camera.common.viewMatrix * camera.common.projectionMatrix;
	}

	Engine::ResolvedCameraView BuildFromOrthographicCamera( const Entity& entity, const TransformComponent& transform,
		OrthographicCameraComponent& camera) {

		// カメラコンポーネントから描画ビューの情報を構築する
		ResolvedCameraView out{};
		out.valid = true;
		out.sourceCamera = entity;
		out.usesManualCamera = false;
		out.cameraPos = transform.worldMatrix.GetTranslationValue();
		out.forward = Vector3(transform.worldMatrix.m[2][0], transform.worldMatrix.m[2][1], transform.worldMatrix.m[2][2]).Normalize();
		out.nearClip = camera.nearClip;
		out.farClip = (std::max)(camera.farClip, out.nearClip + 0.001f);
		out.cullingMask = static_cast<uint32_t>(camera.common.cullingMask);

		out.matrices.inverseViewMatrix = transform.worldMatrix;
		out.matrices.viewMatrix = camera.common.viewMatrix;
		out.matrices.projectionMatrix = camera.common.projectionMatrix;
		out.matrices.inverseProjectionMatrix = Matrix4x4::Inverse(out.matrices.projectionMatrix);
		out.matrices.viewProjectionMatrix = camera.common.viewProjectionMatrix;

		return out;
	}

	Engine::ResolvedCameraView BuildFromPerspectiveCamera(
		const Entity& entity, const TransformComponent& transform, PerspectiveCameraComponent& camera) {

		// カメラコンポーネントから描画ビューの情報を構築する
		ResolvedCameraView out{};
		out.valid = true;
		out.sourceCamera = entity;
		out.usesManualCamera = false;
		out.cameraPos = transform.worldMatrix.GetTranslationValue();
		out.forward = Vector3(transform.worldMatrix.m[2][0], transform.worldMatrix.m[2][1], transform.worldMatrix.m[2][2]).Normalize();
		out.nearClip = (std::max)(camera.nearClip, 0.001f);
		out.farClip = (std::max)(camera.farClip, out.nearClip + 0.001f);
		out.cullingMask = static_cast<uint32_t>(camera.common.cullingMask);

		out.matrices.inverseViewMatrix = transform.worldMatrix;
		out.matrices.viewMatrix = camera.common.viewMatrix;
		out.matrices.projectionMatrix = camera.common.projectionMatrix;
		out.matrices.inverseProjectionMatrix = Matrix4x4::Inverse(out.matrices.projectionMatrix);
		out.matrices.viewProjectionMatrix = camera.common.viewProjectionMatrix;

		return out;
	}

	Engine::ResolvedCameraView BuildManualOrthographic(
		const ManualRenderCameraState& state, uint32_t width, uint32_t height) {

		ResolvedCameraView out{};
		if (!state.enableOrthographic) {
			return out;
		}

		// マニュアルカメラの状態から描画ビューの情報を構築する
		out.valid = true;
		out.usesManualCamera = true;
		out.cameraPos = state.transform2D.pos;
		out.nearClip = state.orthoNearClip;
		out.farClip = (std::max)(state.orthoFarClip, out.nearClip + 0.001f);
		out.cullingMask = static_cast<uint32_t>(state.orthographicCullingMask);

		Matrix4x4 world = BuildManualWorld(state.transform2D);
		out.forward = Vector3(
			world.m[2][0],
			world.m[2][1],
			world.m[2][2]).Normalize();
		out.matrices.inverseViewMatrix = world;
		out.matrices.viewMatrix = Matrix4x4::Inverse(world);
		// 2Dカメラと同様に深度範囲を前後対称にする
		const float orthoDepthRange = (std::max)(out.farClip, 1.0f);
		const float zoom = std::clamp(state.orthographicZoom, 0.01f, 100.0f);
		out.matrices.projectionMatrix = Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f,
			static_cast<float>(width) / zoom, static_cast<float>(height) / zoom,
			-orthoDepthRange, orthoDepthRange);
		out.matrices.inverseProjectionMatrix = Matrix4x4::Inverse(out.matrices.projectionMatrix);
		out.matrices.viewProjectionMatrix = out.matrices.viewMatrix * out.matrices.projectionMatrix;

		return out;
	}

	Engine::ResolvedCameraView BuildManualPerspective(
		const ManualRenderCameraState& state, uint32_t width, uint32_t height) {

		ResolvedCameraView out{};
		if (!state.enablePerspective) {
			return out;
		}

		// マニュアルカメラの状態から描画ビューの情報を構築する
		out.valid = true;
		out.usesManualCamera = true;
		out.cameraPos = state.transform3D.pos;
		out.nearClip = (std::max)(state.perspectiveNearClip, 0.001f);
		out.farClip = (std::max)(state.perspectiveFarClip, out.nearClip + 0.001f);
		out.cullingMask = static_cast<uint32_t>(state.perspectiveCullingMask);

		float aspect = static_cast<float>(width) / static_cast<float>((std::max)(height, 1u));

		Matrix4x4 world = BuildManualWorld(state.transform3D);
		out.forward = Vector3(
			world.m[2][0],
			world.m[2][1],
			world.m[2][2]).Normalize();
		out.matrices.inverseViewMatrix = world;
		out.matrices.viewMatrix = Matrix4x4::Inverse(world);
		out.matrices.projectionMatrix = Matrix4x4::MakePerspectiveFovMatrix(state.perspectiveFovY, aspect, out.nearClip, out.farClip);
		out.matrices.inverseProjectionMatrix = Matrix4x4::Inverse(out.matrices.projectionMatrix);
		out.matrices.viewProjectionMatrix = out.matrices.viewMatrix * out.matrices.projectionMatrix;

		return out;
	}
}
