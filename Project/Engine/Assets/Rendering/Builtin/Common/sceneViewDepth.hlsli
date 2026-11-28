#ifndef NEM_SCENE_VIEW_DEPTH_HLSLI
#define NEM_SCENE_VIEW_DEPTH_HLSLI

// SceneViewとGameViewの投影深度
static const float kSceneNearDepth = 1.0f;
static const float kSceneFarDepth = 0.0f;

// Shader Graphに公開するスクリーン深度は近面を0、遠面を1とする
float SceneDepthToForward(float depth) {
	return 1.0f - depth;
}

float4 SceneRasterPositionToForward(float4 position, bool reversedDepth) {
	if (reversedDepth) position.z = SceneDepthToForward(position.z);
	return position;
}

#endif // NEM_SCENE_VIEW_DEPTH_HLSLI
