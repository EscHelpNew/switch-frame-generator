// SPDX-License-Identifier: GPL-3.0-or-later
#version 460
layout(std140, binding=0) uniform Scene {
    vec4 ball; // normalized x, y, radius, simulation time
    vec4 extent; // width, height, floor y, unused
};
layout(location=0) out vec4 outColor;
void main() {
    vec2 uv = gl_FragCoord.xy / extent.xy;
    vec3 color = mix(vec3(0.035,0.055,0.10), vec3(0.09,0.14,0.20), uv.y);
    vec2 grid = abs(fract(uv * vec2(24.0,14.0)) - 0.5);
    if (max(grid.x, grid.y) > 0.485) color += 0.045;
    if (uv.y >= extent.z) {
        color = vec3(0.12,0.18,0.24);
        if (fract(uv.x*24.0) < 0.025 || fract((uv.y-extent.z)*28.0)<0.03)
            color += 0.10;
    }
    vec2 shadow = (uv-vec2(ball.x,extent.z+0.012))*vec2(1.0,5.0);
    color *= 1.0 - 0.5*exp(-dot(shadow,shadow)/0.003);
    vec2 p = (uv-ball.xy)*vec2(extent.x/extent.y,1.0)/ball.z;
    float d = length(p);
    if (d < 1.0) {
        float z = sqrt(max(0.0,1.0-dot(p,p)));
        float light = clamp(dot(normalize(vec3(p,z)),normalize(vec3(-0.5,-0.6,1.0))),0.0,1.0);
        float pattern = step(0.0,sin((p.x+p.y)*10.0 + ball.w*2.0));
        color = mix(vec3(1.0,0.26,0.045),vec3(1.0,0.8,0.08),pattern)*(0.35+0.65*light);
        if (d > 0.91 || abs(p.x)<0.035 || abs(p.y)<0.035) color = vec3(0.08,0.025,0.015);
    }
    if (abs(uv.y-extent.z)<1.5/extent.y) color=vec3(0.3,0.85,0.9);
    outColor=vec4(color,1.0);
}
