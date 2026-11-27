// Shared contour for the translucent dark body and the emissive shell.
float RailLightningHeadEdge(float2 p, float phase, out float angle) {
    const float2 shape = p - float2(.025f,.028f * sin(phase));
    angle = atan2(shape.y, shape.x + .0001f);
    const float x = cos(angle);
    const float y = sin(angle);
    const float front = max(1.20f*x - 2.03f*y, 1.20f*x + 1.48f*y);
    const float back = max(-1.39f*x - .98f*y, -1.32f*x + 1.21f*y);
    const float planes = max(max(front,back),-1.48f*x);
    const float distortion = .027f * sin(angle * 3.0f + phase) +
        .015f * sin(angle * 9.0f - phase * 1.3f);
    return length(shape) - (rcp(planes) + distortion);
}
