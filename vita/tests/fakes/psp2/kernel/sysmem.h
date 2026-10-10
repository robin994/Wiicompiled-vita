#pragma once
#include <cstdint>
using SceUID=int32_t;
enum { SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW=0x0c80d060 };
SceUID sceKernelAllocMemBlock(const char*,int,unsigned,const void*);
int sceKernelGetMemBlockBase(SceUID,void**);
int sceKernelFreeMemBlock(SceUID);
