#include "DisplayOutputMetadata.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <cmath>

DXGI_HDR_METADATA_HDR10 Engine::BuildDisplayOutputMetadata(float maxLuminanceNits) {

	const float peak = std::clamp(maxLuminanceNits, 80.0f, 10000.0f);
	DXGI_HDR_METADATA_HDR10 metadata{};
	metadata.RedPrimary[0] = 35400;
	metadata.RedPrimary[1] = 14600;
	metadata.GreenPrimary[0] = 8500;
	metadata.GreenPrimary[1] = 39850;
	metadata.BluePrimary[0] = 6550;
	metadata.BluePrimary[1] = 2300;
	metadata.WhitePoint[0] = 15635;
	metadata.WhitePoint[1] = 16450;
	// 最大輝度は1nit単位で通知する
	metadata.MaxMasteringLuminance = static_cast<UINT>(std::lround(peak));
	// 最小輝度は0.0001nit単位で通知する
	metadata.MinMasteringLuminance = 50;
	metadata.MaxContentLightLevel = static_cast<UINT16>(std::lround(peak));
	metadata.MaxFrameAverageLightLevel = static_cast<UINT16>(std::lround(peak * 0.5f));
	return metadata;
}
