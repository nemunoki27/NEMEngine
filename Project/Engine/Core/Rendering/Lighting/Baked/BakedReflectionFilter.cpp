#include "BakedLightingBakeState.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>

// c++
#include <algorithm>
#include <cmath>

namespace {

	using namespace Engine;
	constexpr float kPI = 3.14159265359f;

	// D3Dの面順で方向を生成する
	Vector3 Direction(uint32_t face, Vector2 uv) {

		const float x = uv.x * 2.0f - 1.0f;
		const float y = 1.0f - uv.y * 2.0f;
		if (face == 0) return Vector3::Normalize(Vector3(1.0f, y, -x));
		if (face == 1) return Vector3::Normalize(Vector3(-1.0f, y, x));
		if (face == 2) return Vector3::Normalize(Vector3(x, 1.0f, -y));
		if (face == 3) return Vector3::Normalize(Vector3(x, -1.0f, y));
		if (face == 4) return Vector3::Normalize(Vector3(x, y, 1.0f));
		return Vector3::Normalize(Vector3(-x, y, -1.0f));
	}
	// 入力Cubemapを方向から補間する
	Vector4 Sample(const std::vector<Vector4>& pixels, uint32_t offset, uint32_t size, Vector3 direction) {

		const Vector3 absolute(std::abs(direction.x), std::abs(direction.y), std::abs(direction.z));
		uint32_t face;
		Vector2 st;
		if (absolute.x >= absolute.y && absolute.x >= absolute.z) {
			face = direction.x >= 0.0f ? 0u : 1u;
			st = Vector2(direction.x >= 0.0f ? -direction.z : direction.z, direction.y) / absolute.x;
		} else if (absolute.y >= absolute.z) {
			face = direction.y >= 0.0f ? 2u : 3u;
			st = Vector2(direction.x, direction.y >= 0.0f ? -direction.z : direction.z) / absolute.y;
		} else {
			face = direction.z >= 0.0f ? 4u : 5u;
			st = Vector2(direction.z >= 0.0f ? direction.x : -direction.x, direction.y) / absolute.z;
		}
		const Vector2 position((st.x * 0.5f + 0.5f) * float(size) - 0.5f, (0.5f - st.y * 0.5f) * float(size) - 0.5f);
		const int x = int(std::floor(position.x));
		const int y = int(std::floor(position.y));
		const float wx = position.x - float(x);
		const float wy = position.y - float(y);
		const auto read = [&](int sx, int sy) {
			return pixels[offset + (face * size + uint32_t(std::clamp(sy, 0, int(size) - 1))) * size + uint32_t(std::clamp(sx, 0, int(size) - 1))];
		};
		return (read(x, y) * (1.0f - wx) + read(x + 1, y) * wx) * (1.0f - wy) +
			(read(x, y + 1) * (1.0f - wx) + read(x + 1, y + 1) * wx) * wy;
	}
	// 固定系列の二進数を反転する
	float RadicalInverse(uint32_t index) {

		index = (index << 16) | (index >> 16);
		index = ((index & 0x55555555u) << 1) | ((index & 0xAAAAAAAAu) >> 1);
		index = ((index & 0x33333333u) << 2) | ((index & 0xCCCCCCCCu) >> 2);
		index = ((index & 0x0F0F0F0Fu) << 4) | ((index & 0xF0F0F0F0u) >> 4);
		index = ((index & 0x00FF00FFu) << 8) | ((index & 0xFF00FF00u) >> 8);
		return float(index) * 2.3283064365386963e-10f;
	}
}

void Engine::FilterBakedReflectionCaptures(BakedLightingData& data, const std::atomic<bool>& cancelled) {

	const auto source = std::move(data.reflectionPixels);
	data.reflectionPixels.clear();
	for (auto& capture : data.captures) {
		const uint32_t sourceOffset = capture.dataOffset;
		capture.dataOffset = static_cast<uint32_t>(data.reflectionPixels.size());
		capture.mipCount = 1;
		for (uint32_t size = capture.size; size > 1; size >>= 1) ++capture.mipCount;
		data.reflectionPixels.insert(data.reflectionPixels.end(), source.begin() + sourceOffset,
			source.begin() + sourceOffset + 6u * capture.size * capture.size);
		for (uint32_t mip = 1; mip < capture.mipCount; ++mip) {
			const uint32_t size = std::max(capture.size >> mip, 1u);
			const float roughness = float(mip) / float(capture.mipCount - 1);
			const float alpha = roughness * roughness;
			for (uint32_t face = 0; face < 6; ++face) {
				if (cancelled.load()) return;
				for (uint32_t y = 0; y < size; ++y) {
					for (uint32_t x = 0; x < size; ++x) {
						const Vector3 normal = Direction(face, Vector2((float(x) + 0.5f) / size, (float(y) + 0.5f) / size));
						const Vector3 up = std::abs(normal.z) < 0.999f ? Vector3(0.0f, 0.0f, 1.0f) : Vector3(1.0f, 0.0f, 0.0f);
						const Vector3 tangent = Vector3::Normalize(Vector3::Cross(up, normal));
						const Vector3 bitangent = Vector3::Cross(normal, tangent);
						Vector4 color(0.0f, 0.0f, 0.0f, 0.0f);
						float totalWeight = 0.0f;
						// GGX分布で各粗さの反射を畳み込む
						for (uint32_t sample = 0; sample < 128; ++sample) {
							const float phi = float(sample) / 128.0f * 2.0f * kPI;
							const float sequence = RadicalInverse(sample);
							const float cosine = std::sqrt((1.0f - sequence) / (1.0f + (alpha * alpha - 1.0f) * sequence));
							const float sine = std::sqrt(std::max(1.0f - cosine * cosine, 0.0f));
							const Vector3 halfDirection = tangent * (std::cos(phi) * sine) + bitangent * (std::sin(phi) * sine) + normal * cosine;
							const Vector3 light = halfDirection * (2.0f * Vector3::Dot(normal, halfDirection)) - normal;
							const float weight = std::max(Vector3::Dot(normal, light), 0.0f);
							color += Sample(source, sourceOffset, capture.size, light) * weight;
							totalWeight += weight;
						}
						data.reflectionPixels.push_back(color / std::max(totalWeight, 1e-6f));
					}
				}
			}
		}
	}
}
