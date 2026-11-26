#include "EXRTextureDecoder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>

#include <algorithm>
#include <cstring>
#include <string_view>
#include <d3d12.h>
#include <openexr.h>

namespace {

	int64_t ReadSnapshot(exr_const_context_t, void* user, void* buffer, uint64_t size, uint64_t offset, exr_stream_error_func_ptr_t) {

		const auto& bytes = *static_cast<const std::string*>(user);
		if (offset >= bytes.size()) return 0;
		const size_t count = static_cast<size_t>((std::min)(size, uint64_t(bytes.size() - offset)));
		std::memcpy(buffer, bytes.data() + offset, count);
		return static_cast<int64_t>(count);
	}

	int64_t SnapshotSize(exr_const_context_t, void* user) {

		return static_cast<int64_t>(static_cast<const std::string*>(user)->size());
	}

	int ChannelIndex(std::string_view name) {

		if (name == "Y" || name == "R") return 0;
		if (name == "G") return 1;
		if (name == "B") return 2;
		if (name == "A") return 3;
		return -1;
	}

	template<typename T>
	void CompleteChannels(const DirectX::Image& image, bool grayscale, T one) {

		for (size_t y = 0; y < image.height; ++y) {
			auto* pixel = reinterpret_cast<T*>(image.pixels + image.rowPitch * y);
			for (size_t x = 0; x < image.width; ++x, pixel += 4) {
				if (grayscale) pixel[1] = pixel[2] = pixel[0];
				pixel[3] = one;
			}
		}
	}
}

HRESULT Engine::EXRTextureDecoder::Read(const std::filesystem::path& path, const std::string* snapshot,
	DirectX::TexMetadata& metadata, DirectX::ScratchImage* image, const char*& diagnostic) {

	exr_context_initializer_t options = EXR_DEFAULT_CONTEXT_INITIALIZER;
	options.max_image_width = options.max_image_height = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
	if (snapshot) {
		options.user_data = const_cast<std::string*>(snapshot);
		options.read_fn = ReadSnapshot;
		options.size_fn = SnapshotSize;
	}
	const auto checked = [&](exr_result_t code) {
		if (code == EXR_ERR_SUCCESS) return true;
		diagnostic = exr_get_default_error_message(code);
		return false;
	};
	exr_context_t context = nullptr;
	if (!checked(exr_start_read(&context, Algorithm::PathToUTF8(path).c_str(), &options))) return E_FAIL;
	ScopedCleanup close([&]() noexcept { exr_finish(&context); });

	int parts = 0;
	exr_storage_t storage{};
	if (!checked(exr_get_count(context, &parts)) || !checked(exr_get_storage(context, 0, &storage))) return E_FAIL;
	if (parts != 1 || (storage != EXR_STORAGE_SCANLINE && storage != EXR_STORAGE_TILED)) {
		diagnostic = "EXR: 単一パートの通常の2D画像を指定してください";
		return E_NOTIMPL;
	}
	exr_attr_box2i_t bounds{};
	const exr_attr_chlist_t* channels = nullptr;
	if (!checked(exr_get_data_window(context, 0, &bounds)) || !checked(exr_get_channels(context, 0, &channels))) return E_FAIL;
	bool fullPrecision = false;
	unsigned mask = 0;
	for (int i = 0; i < channels->num_channels; ++i) {
		const auto& channel = channels->entries[i];
		const int index = ChannelIndex(channel.name.str);
		if (index < 0 || (mask & (1u << index)) || channel.x_sampling != 1 || channel.y_sampling != 1 ||
			(channel.pixel_type != EXR_PIXEL_HALF && channel.pixel_type != EXR_PIXEL_FLOAT) ||
			(std::string_view(channel.name.str) == "Y" && channels->num_channels != 1)) {
			diagnostic = "EXR: 等倍サンプリングのHALFまたはFLOATのY、R、RGB、RGBAに対応しています";
			return E_NOTIMPL;
		}
		mask |= 1u << index;
		fullPrecision |= channel.pixel_type == EXR_PIXEL_FLOAT;
	}
	if (mask != 1 && mask != 7 && mask != 15) {
		diagnostic = "EXR: Y、R、RGB、RGBAのチャンネル構成に対応しています";
		return E_NOTIMPL;
	}
	const int64_t width = int64_t(bounds.max.x) - bounds.min.x + 1;
	const int64_t height = int64_t(bounds.max.y) - bounds.min.y + 1;
	if (width < 1 || height < 1 || width > options.max_image_width || height > options.max_image_height) {
		diagnostic = "EXR: 画像サイズが2Dテクスチャの範囲外です";
		return E_INVALIDARG;
	}
	metadata = {};
	metadata.width = static_cast<size_t>(width);
	metadata.height = static_cast<size_t>(height);
	metadata.depth = metadata.arraySize = metadata.mipLevels = 1;
	metadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
	metadata.format = fullPrecision ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;
	if (!image) return S_OK;
	const HRESULT initialized = image->Initialize(metadata);
	if (FAILED(initialized)) return initialized;

	const auto& output = *image->GetImage(0, 0, 0);
	const int bytes = fullPrecision ? 4 : 2;
	const bool tiled = storage == EXR_STORAGE_TILED;
	int32_t stepX = static_cast<int32_t>(width), stepY = 0;
	if (!checked(tiled ? exr_get_tile_sizes(context, 0, 0, 0, &stepX, &stepY) :
		exr_get_scanlines_per_chunk(context, 0, &stepY)) || stepX < 1 || stepY < 1) return E_FAIL;

	exr_decode_pipeline_t pipe = EXR_DECODE_PIPELINE_INITIALIZER;
	ScopedCleanup release([&]() noexcept { exr_decoding_destroy(context, &pipe); });
	for (int64_t y = 0; y < height; y += stepY) {
		for (int64_t x = 0; x < width; x += stepX) {
			exr_chunk_info_t chunk{};
			if (!checked(tiled ? exr_read_tile_chunk_info(context, 0, int(x / stepX), int(y / stepY), 0, 0, &chunk) :
				exr_read_scanline_chunk_info(context, 0, int(y + bounds.min.y), &chunk))) return E_FAIL;
			if (chunk.width < 1 || chunk.height < 1 || x + chunk.width > width || y + chunk.height > height) return E_FAIL;
			const bool first = x == 0 && y == 0;
			if (!checked(first ? exr_decoding_initialize(context, 0, &chunk, &pipe) :
				exr_decoding_update(context, 0, &chunk, &pipe))) return E_FAIL;
			for (int i = 0; i < pipe.channel_count; ++i) {
				auto& channel = pipe.channels[i];
				channel.user_data_type = static_cast<uint16_t>(fullPrecision ? EXR_PIXEL_FLOAT : EXR_PIXEL_HALF);
				channel.user_bytes_per_element = static_cast<int16_t>(bytes);
				channel.user_pixel_stride = 4 * bytes;
				channel.user_line_stride = static_cast<int32_t>(output.rowPitch);
				channel.decode_to_ptr = output.pixels + y * output.rowPitch + (x * 4 + ChannelIndex(channel.channel_name)) * bytes;
			}
			if (first && !checked(exr_decoding_choose_default_routines(context, 0, &pipe))) return E_FAIL;
			if (!checked(exr_decoding_run(context, 0, &pipe))) return E_FAIL;
		}
	}
	if (mask != 15) {
		if (fullPrecision) CompleteChannels(output, mask == 1, 1.0f);
		else CompleteChannels(output, mask == 1, uint16_t{0x3c00});
	}
	return S_OK;
}
