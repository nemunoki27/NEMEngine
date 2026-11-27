#ifndef NEM_SURFACE_GRAPH_MATH
#define NEM_SURFACE_GRAPH_MATH

float4 SurfaceGraphMath(uint op, float4 a, float4 b, float4 c, float4 d, float4 v) {
	switch (op) {
	case 6: v = a[min(uint(v.x),3u)].xxxx; break; // Split
	case 7: v = float4(a.x,b.x,c.x,d.x); break; // Combine
	case 9: v = float4(a.xy*b.xy+c.xy,0,1); break; // TilingAndOffset
	case 10: v = a+b; break; // Add
	case 11: v = a-b; break; // Subtract
	case 12: v = a*b; break; // Multiply
	case 13: v = a/b; break; // Divide
	case 14: v = pow(a,b); break; // Power
	case 15: v = min(a,b); break; // Minimum
	case 16: v = max(a,b); break; // Maximum
	case 17: v = 1-a; break; // OneMinus
	case 18: v = lerp(a,b,c); break; // Lerp
	case 19: v = saturate(a); break; // Saturate
	case 20: v = sin(a); break; // Sine
	case 21: v = cos(a); break; // Cosine
	case 22: v = abs(a); break; // Absolute
	case 23: v = floor(a); break; // Floor
	case 24: v = frac(a); break; // Fraction
	case 25: v = sqrt(a); break; // SquareRoot
	case 26: v = -a; break; // Negate
	case 27: v = step(a,b); break; // Step
	case 28: v = smoothstep(a,b,c); break; // Smoothstep
	case 29: v = clamp(a,b,c); break; // Clamp
	case 30: v = a.x != 0 ? b : c; break; // Branch
	case 31: v = ceil(a); break; // Ceiling
	case 32: v = round(a); break; // Round
	case 33: v = sign(a); break; // Sign
	case 34: v = trunc(a); break; // Truncate
	case 35: v = tan(a); break; // Tangent
	case 36: v = rcp(a); break; // Reciprocal
	case 37: v = v.x == 1 ? exp2(a) : (v.x == 2 ? pow(10.0f, a) : exp(a)); break; // Exponential
	case 38: v = v.x == 1 ? log2(a) : (v.x == 2 ? log10(a) : log(a)); break; // Log
	case 39: v = fmod(a,b); break; // Modulo
	}
	return v;
}
#endif
