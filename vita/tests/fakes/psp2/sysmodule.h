#pragma once
#include <cstdint>
enum { SCE_SYSMODULE_AVCDEC=0x25 };
int sceSysmoduleIsLoaded(uint16_t);
int sceSysmoduleLoadModule(uint16_t);
int sceSysmoduleUnloadModule(uint16_t);
