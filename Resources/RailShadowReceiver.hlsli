#ifndef GE3_RAIL_SHADOW_RECEIVER
#define GE3_RAIL_SHADOW_RECEIVER

// SceneColor can use UNORM8. Keep the tag exactly representable and reserve
// it for the opaque carrier; the near-shadow pass restores alpha to one.
static const float kRailShadowCasterOnly = 17.0f / 255.0f;

#endif
