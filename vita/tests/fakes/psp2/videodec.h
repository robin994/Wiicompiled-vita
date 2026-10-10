#pragma once
#include <cstdint>
// Host lifecycle test doubles. ARM ABI is checked by compiling the real SDK.
enum SceVideodecType { SCE_VIDEODEC_TYPE_HW_AVCDEC=0x1001 };
enum { SCE_AVCDEC_PIXELFORMAT_YUV420_PACKED_RASTER=0x20 };
struct SceVideodecQueryInitInfoHwAvcdec { uint32_t size,horizontal,vertical,numOfRefFrames,numOfStreams; };
struct SceAvcdecQueryDecoderInfo { uint32_t horizontal,vertical,numOfRefFrames; };
struct SceAvcdecDecoderInfo { uint32_t frameMemSize; };
struct SceAvcdecBuf { void* pBuf;uint32_t size; };
struct SceAvcdecCtrl { uint32_t handle;SceAvcdecBuf frameBuf; };
struct SceVideodecTimeStamp { uint32_t upper,lower; };
struct SceAvcdecAu { SceVideodecTimeStamp pts,dts;SceAvcdecBuf es; };
struct SceAvcdecFrame {
    uint32_t pixelType,framePitch,frameWidth,frameHeight,horizontalSize,verticalSize;
    uint32_t frameCropLeftOffset,frameCropRightOffset,frameCropTopOffset,frameCropBottomOffset;
    uint8_t opt[16];void* pPicture[2];
};
struct SceAvcdecPicture { uint32_t size;SceAvcdecFrame frame;uint8_t info[32]; };
struct SceAvcdecArrayPicture { uint32_t numOfOutput,numOfElm;SceAvcdecPicture** pPicture; };
int sceVideodecInitLibrary(SceVideodecType,const SceVideodecQueryInitInfoHwAvcdec*);
int sceVideodecTermLibrary(SceVideodecType);
int sceAvcdecQueryDecoderMemSize(SceVideodecType,const SceAvcdecQueryDecoderInfo*,SceAvcdecDecoderInfo*);
int sceAvcdecCreateDecoder(SceVideodecType,SceAvcdecCtrl*,const SceAvcdecQueryDecoderInfo*);
int sceAvcdecDeleteDecoder(SceAvcdecCtrl*);
int sceAvcdecDecode(const SceAvcdecCtrl*,const SceAvcdecAu*,SceAvcdecArrayPicture*);
