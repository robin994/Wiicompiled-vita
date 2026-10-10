#pragma once
#include <cstdint>
#include "vita_render_options.h"
namespace wiicompiled::backend {
bool initialize(bool diagnosticsEnabled=true,const VitaRenderOptions& options={});
void shutdown();
bool beginFrame();
void endFrame();
void rectangle(float x,float y,float width,float height,uint8_t r,uint8_t g,uint8_t b);
void text(float x,float y,const char* value,float scale=3);
}
