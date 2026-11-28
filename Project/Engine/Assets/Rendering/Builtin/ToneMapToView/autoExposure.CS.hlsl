//============================================================================
//	resources
//============================================================================
Texture2D<float4> gSourceColor : register(t0);
RWStructuredBuffer<float4> gExposureState : register(u0);

//============================================================================
//	constants
//============================================================================
cbuffer ColorPipelineConstants : register(b0) {

	uint width;
	uint height;
	uint exposureMode;
	uint resetExposure;

	float manualEV100;
	float exposureCompensation;
	float minEV100;
	float maxEV100;

	float histogramLowPercent;
	float histogramHighPercent;
	float speedUp;
	float speedDown;

	float deltaTime;
	float usePreExposure;
	float filmicSlope;
	float filmicToe;

	float filmicShoulder;
	float filmicBlackClip;
	float filmicWhiteClip;
	float postProcessEnabled;

	float4 colorFilter;
	float3 whiteBalance;
	float _pad1;
	float3 saturation;
	float _pad2;
	float3 contrast;
	float _pad3;
	float3 gamma;
	float _pad4;
	float3 gain;
	float _pad5;
	float3 offset;
	float _pad6;

	uint outputMode;
	float paperWhiteNits;
	float maxLuminanceNits;
	uint diffuseGIDebug;
};

//============================================================================
//	constants
//============================================================================
static const uint kHistogramBinCount = 64;
static const uint kSampleWidth = 64;
static const uint kSampleHeight = 36;
static const float kMinimumLogLuminance = -12.0f;
static const float kMaximumLogLuminance = 16.0f;
static const float kMiddleGray = 0.18f;
static const float kTransitionEV = 1.5f;

groupshared uint gHistogram[kHistogramBinCount];

//============================================================================
//	main
//============================================================================
[numthreads(256, 1, 1)]
void main(uint groupIndex : SV_GroupIndex) {

	if (exposureMode == 0) {

		if (groupIndex == 0) {
			const float targetEV100 = clamp(
				manualEV100 - exposureCompensation, -20.0f, 30.0f);
			const float targetExposure = exp2(-targetEV100);
			const float previousExposure = gExposureState[0].x;
			gExposureState[0] = float4(targetExposure,
				resetExposure != 0 || previousExposure <= 0.0f ? targetExposure : previousExposure,
				targetEV100, targetEV100);
		}
		return;
	}

	if (groupIndex < kHistogramBinCount) {
		gHistogram[groupIndex] = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	const uint sampleWidth = min(width, kSampleWidth);
	const uint sampleHeight = min(height, kSampleHeight);
	const uint sampleCount = sampleWidth * sampleHeight;
	for (uint sampleIndex = groupIndex; sampleIndex < sampleCount; sampleIndex += 256) {

		const uint sampleX = sampleIndex % sampleWidth;
		const uint sampleY = sampleIndex / sampleWidth;
		const uint sourceX = min(uint((float(sampleX) + 0.5f) * float(width) /
			float(sampleWidth)), width - 1);
		const uint sourceY = min(uint((float(sampleY) + 0.5f) * float(height) /
			float(sampleHeight)), height - 1);
		const float3 color = max(gSourceColor.Load(int3(sourceX, sourceY, 0)).rgb, 0.0f);
		const float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
		if (1.0e-6f <= luminance) {

			const float normalized = saturate((log2(luminance) - kMinimumLogLuminance) /
				(kMaximumLogLuminance - kMinimumLogLuminance));
			const uint bin = min(uint(normalized * float(kHistogramBinCount)),
				kHistogramBinCount - 1);
			InterlockedAdd(gHistogram[bin], 1);
		}
	}
	GroupMemoryBarrierWithGroupSync();

	if (groupIndex != 0) {
		return;
	}

	const float4 previousState = gExposureState[0];
	const bool hasHistory = resetExposure == 0 && 0.0f < previousState.x;
	float targetEV100 = hasHistory ? previousState.w : manualEV100 - exposureCompensation;
	{

		uint totalCount = 0;
		for (uint bin = 0; bin < kHistogramBinCount; ++bin) {
			totalCount += gHistogram[bin];
		}
		if (0 < totalCount) {

			const uint lowCount = uint(float(totalCount) * histogramLowPercent);
			const uint highCount = max(lowCount + 1,
				uint(float(totalCount) * histogramHighPercent));
			uint cumulative = 0;
			float weightedLogLuminance = 0.0f;
			uint weightedCount = 0;
			for (uint bin = 0; bin < kHistogramBinCount; ++bin) {

				const uint previous = cumulative;
				cumulative += gHistogram[bin];
				const uint acceptedStart = max(previous, lowCount);
				const uint acceptedEnd = min(cumulative, highCount);
				if (acceptedStart < acceptedEnd) {

					const uint accepted = acceptedEnd - acceptedStart;
					const float binCenter = (float(bin) + 0.5f) /
						float(kHistogramBinCount);
					const float logLuminance = lerp(kMinimumLogLuminance,
						kMaximumLogLuminance, binCenter);
					weightedLogLuminance += logLuminance * float(accepted);
					weightedCount += accepted;
				}
			}
			if (0 < weightedCount) {
				const float averageLuminance = exp2(weightedLogLuminance /
					float(weightedCount));
				targetEV100 = log2(max(averageLuminance, 1.0e-6f) /
					kMiddleGray) - exposureCompensation;
			}
		}
	}

	targetEV100 = clamp(targetEV100, minEV100, maxEV100);
	float currentEV100 = targetEV100;
	if (hasHistory) {
		const float difference = targetEV100 - previousState.w;
		const float speed = difference > 0.0f ? speedUp : speedDown;
		const float stepEV = speed * max(deltaTime, 0.0f);
		const float distanceEV = abs(difference);
		// 遠方は等速、目標付近は境界で速度が連続する指数順応
		const float linearStep = min(stepEV, max(distanceEV - kTransitionEV, 0.0f));
		const float remaining = (distanceEV - linearStep) * exp(-(stepEV - linearStep) / kTransitionEV);
		currentEV100 -= sign(difference) * remaining;
	}
	const float currentExposure = exp2(-currentEV100);
	gExposureState[0] = float4(currentExposure,
		hasHistory ? previousState.x : currentExposure,
		targetEV100, currentEV100);
}
