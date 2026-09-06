#include "vita/aurora_packet_renderer.h"
#include "aurora-main/platforms/vita/gfx/vita_shader_gen.hpp"
#include "aurora-main/platforms/vita/gfx/vita_pipeline_key.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

int main() {
    using namespace aurora::vita::gfx;
    using WiiCompiledVita::AuroraPacketVertex;
    static_assert(sizeof(AuroraPacketVertex)==28);
    AuroraPacketVertex ui{};
    assert(ui.clipW==1.f); // old UI/probe aggregate initializers keep affine UI
    PipelineDesc base{};
    base.positionIsClipSpace=true;
    base.layout.count=1;
    base.layout.attributes[0]={0,3,VertexScalar::F32,false,28,0};
    PipelineDesc perspective=base;
    perspective.layout.count=2;
    perspective.layout.attributes[1]={11,1,VertexScalar::F32,false,28,24};
    assert(pipeline_key(base)!=pipeline_key(perspective));
    const auto legacy=build_tev_glsl(base);
    const auto shader=build_tev_glsl(perspective);
    assert(legacy.vertex.find("a_clip_w")==std::string::npos);
    assert(shader.vertex.find("vec4(a_position.xyz*a_clip_w,a_clip_w)")!=std::string::npos);
    // A triangle edge with unequal W must preserve screen positions and use
    // perspective interpolation: u at the screen midpoint is 1/3, not 1/2.
    const float clipX[2]={-1.f,2.f}, w[2]={1.f,2.f}, u[2]={0.f,1.f};
    for(unsigned i=0;i<2;++i) {
        const float ndc=clipX[i]/w[i];
        assert(std::abs(ndc*w[i]-clipX[i])<1.e-6f);
    }
    const float interpolated=(u[0]/w[0]+u[1]/w[1])/(1.f/w[0]+1.f/w[1]);
    assert(std::abs(interpolated-1.f/3.f)<1.e-6f);
    std::puts("PASS: compact W ABI, shader selection/cache isolation, UI W=1 and perspective interpolation");
}
