// Reuse the real rigid mesh and final scene depth. No materials, lighting,
// texture samples or depth writes: enemies/cart retain their exact silhouette.
float4 main() : SV_TARGET0 { return 1.0f; }
