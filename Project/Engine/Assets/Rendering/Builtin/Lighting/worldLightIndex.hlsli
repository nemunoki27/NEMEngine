#ifndef NEM_WORLD_LIGHT_INDEX_HLSLI
#define NEM_WORLD_LIGHT_INDEX_HLSLI

struct WorldLightNode {
	float3 lower;
	uint next;
	float3 upper;
	uint light;
};

struct WorldLightBlock {
	uint words[8];
};

StructuredBuffer<WorldLightBlock> gWorldLightNodes : register(t0, space41);

WorldLightNode ReadWorldLightNode(uint index) {
	WorldLightBlock block=gWorldLightNodes[index];
	WorldLightNode node;
	node.lower=asfloat(uint3(block.words[0],block.words[1],block.words[2]));
	node.next=block.words[3];
	node.upper=asfloat(uint3(block.words[4],block.words[5],block.words[6]));
	node.light=block.words[7];
	return node;
}

uint WorldCellLightAt(uint list,uint i) {
	return gWorldLightNodes[list+(i>>3u)].words[i&7u];
}

struct WorldLightCursor {
	uint current;
	uint end;
	uint list;
};

WorldLightCursor BeginWorldLights(float3 position) {
	WorldLightCursor it = (WorldLightCursor)0;
	it.end = localCount;
	if (localCount < 8u) return it;
	WorldLightNode root = ReadWorldLightNode(0);
	if (root.light != 0xfffffffeu) {
		it.end = localCount * 2u - 1u;
		return it;
	}
	uint side = root.next;
	it.list = 1u + ((side * side * side + 8u) >> 3u);
	float3 grid = (position - root.lower) * root.upper;
	it.end = 0u;
	if (any(grid < 0.0f) || any(grid >= float(side))) return it;
	uint3 cell = uint3(grid);
	uint index = cell.x + side * (cell.y + side * cell.z);
	it.current = WorldCellLightAt(1u,index);
	it.end = WorldCellLightAt(1u,index+1u);
	return it;
}


bool NextWorldLight(float3 position, inout WorldLightCursor it, out uint light) {
	light = 0u;
	if (it.current >= it.end) return false;
	if (it.list != 0u) {
		light = WorldCellLightAt(it.list, it.current++);
		return true;
	}
	if (localCount < 8u) {
		light = it.current++;
		return true;
	}
	[loop]
	while (it.current < it.end) {
		WorldLightNode node = ReadWorldLightNode(it.current);
		if (any(position < node.lower) || any(position > node.upper)) {
			it.current = node.next;
			continue;
		}
		++it.current;
		if (node.light != 0xffffffffu) {
			light = node.light;
			return true;
		}
	}
	return false;
}

#endif

