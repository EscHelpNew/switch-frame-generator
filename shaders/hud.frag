// SPDX-License-Identifier: GPL-3.0-or-later
#version 460
layout(std140,binding=0) uniform Hud { uvec4 text[80]; };
layout(location=0) out vec4 outColor;
// 5x6 bitmap glyphs, one five-bit row per group of bits.
uint glyph(uint c) {
    switch(c) {
    case 65u:return 0x2318FE2Eu; case 66u:return 0x1F18BE2Fu;
    case 67u:return 0x3C10843Eu; case 68u:return 0x1F18C62Fu;
    case 69u:return 0x3E10BC3Fu; case 70u:return 0x0210BC3Fu;
    case 71u:return 0x3D18F43Eu; case 72u:return 0x2318FE31u;
    case 73u:return 0x3E42109Fu; case 74u:return 0x0C94211Cu;
    case 75u:return 0x22929D31u; case 76u:return 0x3E108421u;
    case 77u:return 0x2318D771u; case 78u:return 0x231CD671u;
    case 79u:return 0x1D18C62Eu; case 80u:return 0x0217C62Fu;
    case 81u:return 0x2C9AC62Eu; case 82u:return 0x2292BE2Fu;
    case 83u:return 0x1F08383Eu; case 84u:return 0x0842109Fu;
    case 85u:return 0x1D18C631u; case 86u:return 0x08A8C631u;
    case 87u:return 0x23BAC631u; case 88u:return 0x22A21151u;
    case 89u:return 0x08421151u; case 90u:return 0x3E11111Fu;
    case 48u:return 0x1D19D72Eu; case 49u:return 0x1C4210C4u;
    case 50u:return 0x3E22222Eu; case 51u:return 0x1F08320Fu;
    case 52u:return 0x21087E31u; case 53u:return 0x1F083C3Fu;
    case 54u:return 0x1D18BC3Eu; case 55u:return 0x0842221Fu;
    case 56u:return 0x1D18BA2Eu; case 57u:return 0x1F087A2Eu;
    case 46u:return 0x08400000u; case 58u:return 0x08401080u;
    case 47u:return 0x02221110u; case 43u:return 0x00023880u;
    case 45u:return 0x00007C00u; case 62u:return 0x04442082u;
    case 95u:return 0x3E000000u; default:return 0u;
    }
}
void main() {
    ivec2 p=ivec2(gl_FragCoord.xy)-ivec2(16,12);
    if(p.x<0 || p.y<0 || p.x>=1152 || p.y>=120) discard;
    int col=p.x/18, row=p.y/24;
    int i=row*64+col;
    // The native Maxwell/UAM path miscompiles dynamic vector-component reads
    // here, repeating lane x four times. Keep component selection scalar.
    uvec4 characters=text[i/4];
    int lane=i%4;
    uint c=characters.x;
    if(lane==1) c=characters.y;
    if(lane==2) c=characters.z;
    if(lane==3) c=characters.w;
    ivec2 q=ivec2((p.x%18)/3,(p.y%24)/3);
    bool ink=q.x<5 && q.y<6 && ((glyph(c) >> uint(q.y*5+q.x)) & 1u)!=0u;
    outColor=ink ? vec4(0.8,0.96,1.0,1.0) : vec4(0.015,0.025,0.04,1.0);
}
