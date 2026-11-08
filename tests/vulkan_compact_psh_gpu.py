#!/usr/bin/env python3
"""Build a compute differential test from the *actual* generated fragment bodies.
Run vulkan_compact_psh with two output paths first; compile this output with
 glslang -V --target-env vulkan1.1 -S comp -o compare.spv compare.comp
then run vulkan_compute_check compare.spv on the Android device.
Texture sampling is replaced by identical synthetic texels. This checks combiner,
fog, saturation and alpha semantics, not rasterization/filtering/performance.
"""
import pathlib
import sys

def function(path, name):
    source = pathlib.Path(path).read_text()
    body = source.split('void main() {', 1)[1].rsplit('}', 1)[0]
    body = body.replace('texture(t0, tc0 * u_tscale[0])', 'texel')
    body = body.replace('return;', 'return fragColor;')
    body = body.replace('if (!pass) discard;', 'if (!pass) return vec4(-2.0);')
    return ('vec4 ' + name + '(vec4 texel, vec4 vD0, vec4 vD1, float vFog, '
            'vec4 u_fc0, int u_alpha_func, float u_alpha_ref, '
            'bool nfsu_alpha_test, bool nfsu_zero_source) {\nvec4 fragColor;\n'
            + body + '\nreturn fragColor;\n}\n')

print('''#version 450
layout(local_size_x=64) in;
layout(std430, set=0, binding=0) buffer Result { uint failures; uint cases; uint first_case; uint unused; };
const vec4 vT0 = vec4(0.0, 0.0, 0.0, 1.0);
const vec2 u_tscale[4] = vec2[4](vec2(1.0),vec2(1.0),vec2(1.0),vec2(1.0));
const vec4 u_fogcolor = vec4(0.0);
''')
print(function(sys.argv[1], 'reference'))
print(function(sys.argv[2], 'compact'))
print('''
uint random_bits(inout uint seed) { seed ^= seed<<13; seed ^= seed>>17; seed ^= seed<<5; return seed; }
float sample_value(inout uint seed) {
    uint bits = random_bits(seed);
    // Finite values, including signed zero, tiny/large inputs and clamp edges.
    float edges[12] = float[12](-0.0,0.0,-1.0,1.0,0.5,1e-8,-1e-8,1e-30,-1e-30,1e20,-1e20,255.0);
    return (bits & 3u) == 0u ? edges[(bits>>2)%12] : float(int(bits%16385)-8192)/4096.0;
}
vec4 sample_vec(inout uint seed) {
    float a=sample_value(seed), b=sample_value(seed), c=sample_value(seed), d=sample_value(seed);
    return vec4(a,b,c,d);
}
void main() {
    uint id=gl_GlobalInvocationID.x;
    uint seed=id+1;
    for (uint trial=0; trial<64; trial++) {
        vec4 texel=sample_vec(seed), diffuse=sample_vec(seed), specular=sample_vec(seed);
        vec4 fogcolor=sample_vec(seed);
        float fog=sample_value(seed);
        int func=0x200+int(random_bits(seed)%9); // all comparisons and unknown/pass
        float ref=sample_value(seed)*255.0;
        bool alpha=(random_bits(seed)&1u)!=0u, zero=(random_bits(seed)&15u)==0u;
        vec4 a=reference(texel,diffuse,specular,fog,fogcolor,func,ref,alpha,zero);
        vec4 b=compact(texel,diffuse,specular,fog,fogcolor,func,ref,alpha,zero);
        // Numeric equality treats +/-0 alike. Also compare NaN masks if an
        // implementation ever creates one; test inputs themselves are finite.
        if (any(notEqual(isnan(a),isnan(b))) || any(notEqual(a,b))) {
            atomicAdd(failures,1);
            atomicMin(first_case,id*64+trial);
        }
        atomicAdd(cases,1);
    }
}
''')
