#pragma once

#include <stdint.h>
#include "RW/RenderWare.h"

#define NEAR_SCR "_ZN9CSprite2d11NearScreenZE"

enum eGTA : char
{
    WRONG = 0,
    GTA3 = 1,
    GTAVC = 2,
    GTASA = 3,
};

extern uintptr_t pGameLib;
extern void* pGameHandle;
extern eGTA nLoadedGTA;

extern RwReal* nearScreenZ;
extern RwReal* recipNearClip;
extern void (*SetScissorRect)(float*);
