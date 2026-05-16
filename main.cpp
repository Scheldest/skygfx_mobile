#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include <string>
#include <math.h>
#include "main.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// Deklarasi Mod
MYMODCFG(net.dexsociety.camstatic, CameraStatic, 1.0, Dexsociety)

uintptr_t pGameLib = 0;
void* pGameHandle = nullptr;
eGTA nLoadedGTA = WRONG;

// --- VARIABEL GAME ---
float* timeStep = nullptr;
RwReal* nearScreenZ;
RwReal* recipNearClip;
void (*SetScissorRect)(float*);

float smoothH = 0.0f;
float smoothV = 0.0f;
bool firstInit = true;

float velH = 0.0f; 
float velV = 0.0f;

// Pencatat waktu toleransi filter input
float stopTimer = 0.0f;
const float STOP_THRESHOLD = 0.05f;

// --- TOUCH TRACKING (PER-FINGER) ---
float fingerDeltaX[10] = {0}, fingerDeltaY[10] = {0};
float lastTouchX[10] = {0}, lastTouchY[10] = {0};
bool bLookWidgetTouched = false; 
uintptr_t* pWidgets = nullptr;

struct CamSettings {
    float camsensX = 40.0f;
    float camsensY = 35.0f;
    float aimsensX = 30.0f;
    float aimsensY = 25.0f;
    float smoothness = 7.0f;
};

CamSettings settings;

struct CWidget {
    uintptr_t vtable;
    char padding[68];
    bool bIsTouched;    // 0x48 (72)
    char padding2[47];
    int nTouchID;       // 0x78 (120)
};

struct CCam {
    char padding[14];
    uint16_t nMode;         // 0x0E
    char padding1[132 - 16];
    float fAngleVertical;   // 0x84
    float fBetaSpeed;       // 0x88
    char padding2[8];
    float fAngleHorizontal; // 0x94
    float fAlphaSpeed;      // 0x98
    char padding3[528 - 156];
};

struct CCamera {
    char padding[87];
    uint8_t nActiveCam;              // 0x57
    char padding2[172 - 88];
    uint32_t nControlMode;           // 0xAC
    char padding3[368 - 176];
    CCam aCams[3];
};

CCamera* pTheCamera = nullptr;
int* m_bMenuOpened;
bool* m_UserPause;

// --- UTILS ---
inline float normalize(float angle) {
    while (angle > M_PI) angle -= (2.0f * M_PI);
    while (angle < -M_PI) angle += (2.0f * M_PI);
    return angle;
}

// --- SCALING ---
static int nDisplayX, nDisplayY;

static float ClampSetting(float value, float min, float max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

void LoadSettings()
{
    cfg->Init();

    settings.camsensX = ClampSetting(cfg->GetFloat("camSensX", settings.camsensX, "Camera"), 1.0f, 50.0f);
    settings.camsensY = ClampSetting(cfg->GetFloat("camSensY", settings.camsensY, "Camera"), 1.0f, 50.0f);
    settings.aimsensX = ClampSetting(cfg->GetFloat("aimSensX", settings.aimsensX, "Camera"), 1.0f, 50.0f);
    settings.aimsensY = ClampSetting(cfg->GetFloat("aimSensY", settings.aimsensY, "Camera"), 1.0f, 50.0f);
    settings.smoothness = ClampSetting(cfg->GetFloat("Smoothness", settings.smoothness, "Camera"), 1.0f, 10.0f);

    logger->Info("Config loaded: camSensX=%.2f camSensY=%.2f aimSensX=%.2f aimSensY=%.2f Smoothness=%.2f",
                 settings.camsensX, settings.camsensY, settings.aimsensX, settings.aimsensY, settings.smoothness);
}

// --- HELPER MATHEMATICS ---
float SmoothDampAngle(float current, float target, float& currentVelocity, float smoothTime, float deltaTime) 
{
    float diff = target - current;
    diff = atan2f(sinf(diff), cosf(diff));
    
    float targetSanitized = current + diff;
    
    smoothTime = fmaxf(0.0001f, smoothTime);
    float omega = 2.0f / smoothTime;
    
    float x = omega * deltaTime;
    float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    
    float change = current - targetSanitized;
    float originalTo = targetSanitized;
    
    float maxChange = 10.0f * smoothTime; 
    change = fminf(fmaxf(change, -maxChange), maxChange);
    
    float temp = (currentVelocity + omega * change) * deltaTime;
    currentVelocity = (currentVelocity - omega * temp) * exp;
    
    float result = targetSanitized + (change + temp) * exp;
    
    if ((originalTo - current > 0.0f) == (result > originalTo)) {
        result = originalTo;
        currentVelocity = 0.0f;
    }
    
    return result;
}

// --- LOGIKA UTAMA KAMERA ---
void ProcessSmoothCamera()
{
    if (!pTheCamera || !timeStep || !pWidgets || (*m_bMenuOpened != 0) || *m_UserPause) {
        for(int i=0; i<10; ++i) { fingerDeltaX[i] = 0; fingerDeltaY[i] = 0; }
        return;
    }

    uint8_t activeIdx = pTheCamera->nActiveCam;
    if (activeIdx >= 3) return;

    CCam& cam = pTheCamera->aCams[activeIdx];

    // Cek status touch widget secara realtime
    CWidget* pLookWidget = (CWidget*)pWidgets[175];
    bool isCurrentlyTouched = bLookWidgetTouched;
    if (pLookWidget && pLookWidget->bIsTouched) isCurrentlyTouched = true;

    // Paksa kontrol kamera game agar tidak bentrok
    pTheCamera->nControlMode = 1;
    cam.fAlphaSpeed = 0.0f;
    cam.fBetaSpeed = 0.0f;

    if (firstInit) {
        smoothH = cam.fAngleHorizontal;
        smoothV = cam.fAngleVertical;
        firstInit = false;
    }

    float dt = *timeStep * 0.02f; 
    if (dt <= 0.0f) return;

    bool isMoving = false;

    if (isCurrentlyTouched)
    {
        int fingerId = pLookWidget ? pLookWidget->nTouchID : -1;

        // Filter jari analog kiri
        if (fingerId >= 0 && fingerId < 10 && lastTouchX[fingerId] < (float)nDisplayX * 0.45f) {
            fingerId = -1;
        }

        // Fallback cari jari aktif di kanan
        if (fingerId < 0 || fingerId >= 10)
        {
            for(int i=0; i<10; ++i) {
                if((fabsf(fingerDeltaX[i]) > 0.01f || fabsf(fingerDeltaY[i]) > 0.01f) &&
                   (lastTouchX[i] > (float)nDisplayX * 0.45f)) {
                    fingerId = i;
                    break;
                }
            }
        }

        if (fingerId >= 0 && fingerId < 10)
        {
            float dx = fingerDeltaX[fingerId];
            float dy = fingerDeltaY[fingerId];

            if (fabsf(dx) > 0.0f || fabsf(dy) > 0.0f)
            {
                isMoving = true; 

                bool isAimMode = (cam.nMode == 5 || cam.nMode == 7 || cam.nMode == 8 ||
                                  cam.nMode == 16 || cam.nMode == 53 || cam.nMode == 65);

                float sensMultiplier = 0.00025f; 
                float sensX = (isAimMode ? settings.aimsensX : settings.camsensX) * sensMultiplier;
                float sensY = (isAimMode ? settings.aimsensY : settings.camsensY) * sensMultiplier;

                smoothH = normalize(smoothH - (dx * sensX));
                smoothV = smoothV - (dy * sensY);

                if (smoothV > 1.5f)  smoothV = 1.5f;
                if (smoothV < -1.1f) smoothV = -1.1f;
            }
        }
    }

    // --- MANAJEMEN TIMER TOLERANSI JITTER ---
    if (isMoving && isCurrentlyTouched)
    {
        stopTimer = 0.0f; 
    }
    else
    {
        stopTimer += dt; 
    }

    // --- LOGIKA INTEGRASI ---
    if (!isCurrentlyTouched)
    {
        smoothH = cam.fAngleHorizontal;
        smoothV = cam.fAngleVertical;
        velH = 0.0f;
        velV = 0.0f;
    }
    else if (stopTimer >= STOP_THRESHOLD)
    {
        smoothH = cam.fAngleHorizontal;
        smoothV = cam.fAngleVertical;
        velH = 0.0f;
        velV = 0.0f;
    }
    else 
    {
        float dynamicSmoothTime = 0.4f / fmaxf(0.1f, settings.smoothness);

        cam.fAngleHorizontal = normalize(SmoothDampAngle(cam.fAngleHorizontal, smoothH, velH, dynamicSmoothTime, dt));
        cam.fAngleVertical   = SmoothDampAngle(cam.fAngleVertical, smoothV, velV, dynamicSmoothTime, dt);
    }

    if (cam.fAngleVertical > 1.5f)  cam.fAngleVertical = 1.5f;
    if (cam.fAngleVertical < -1.1f) cam.fAngleVertical = -1.1f;

    for(int i=0; i<10; ++i) { fingerDeltaX[i] = 0; fingerDeltaY[i] = 0; }
}

DECL_HOOK(bool, InitRenderware)
{
    if(!InitRenderware()) return false;
    InitRenderWareFunctions();
    if (RsGlobal)
    {
        nDisplayX = RsGlobal->maximumWidth;
        nDisplayY = RsGlobal->maximumHeight;
    }
    return true;
}

DECL_HOOKv(Render2DStuff)
{
    Render2DStuff();
    ProcessSmoothCamera();
}

DECL_HOOKi(IsTouched, int widgetId, void* a2, int a3)
{
    int res = IsTouched(widgetId, a2, a3);
    if (widgetId == 175) bLookWidgetTouched = (res != 0);
    return res;
}

DECL_HOOKv(OnTouchEvent, int type, int fingerId, int x, int y)
{
    if (fingerId >= 0 && fingerId < 10)
    {
        if (type == 2) { // DOWN
            lastTouchX[fingerId] = (float)x;
            lastTouchY[fingerId] = (float)y;
            fingerDeltaX[fingerId] = 0;
            fingerDeltaY[fingerId] = 0;
        }
        else if (type == 3) { // MOVE
            fingerDeltaX[fingerId] += (float)(x - lastTouchX[fingerId]);
            fingerDeltaY[fingerId] += (float)(y - lastTouchY[fingerId]);
            lastTouchX[fingerId] = (float)x;
            lastTouchY[fingerId] = (float)y;
        }
        else if (type == 1) { // UP
            fingerDeltaX[fingerId] = 0;
            fingerDeltaY[fingerId] = 0;
        }
    }

    OnTouchEvent(type, fingerId, x, y);
}

void ApplyNoopPatches()
{
    unsigned char nop[4] = {0x00, 0xBF, 0x00, 0xBF};
    aml->Write(pGameLib + 0x3C39B8, (uintptr_t)nop, 4);
    aml->Write(pGameLib + 0x3C4090, (uintptr_t)nop, 4);
    aml->Write(pGameLib + 0x3C1A72, (uintptr_t)nop, 4);
    aml->Write(pGameLib + 0x3C1778, (uintptr_t)nop, 4);
}

extern "C" void OnModPreLoad()
{
    pGameLib = aml->GetLib("libGTASA.so");
    if(pGameLib)
    {
        pGameHandle = aml->GetLibHandle("libGTASA.so");
        nLoadedGTA = GTASA;
        SET_TO(nearScreenZ, aml->GetSym(pGameHandle, "_ZN9CSprite2d11NearScreenZE"));
        SET_TO(nearScreenZ, aml->GetSym(pGameHandle, "_ZN9CSprite2d11NearScreenZE"));
        SET_TO(recipNearClip, aml->GetSym(pGameHandle, "_ZN9CSprite2d13RecipNearClipE"));
        SET_TO(SetScissorRect, aml->GetSym(pGameHandle, "_ZN7CWidget10SetScissorER5CRect"));
        SET_TO(pTheCamera, aml->GetSym(pGameHandle, "TheCamera"));
        SET_TO(timeStep, aml->GetSym(pGameHandle, "_ZN6CTimer12ms_fTimeStepE"));
    }
}

extern "C" void OnModLoad()
{
    if(nLoadedGTA == GTASA)
    {
        LoadSettings();
        ApplyNoopPatches();
        pWidgets = (uintptr_t*)aml->GetSym(pGameHandle, "_ZN15CTouchInterface10m_pWidgetsE");
        HOOKPLT(InitRenderware, pGameLib + 0x66F2D0);
        HOOKPLT(OnTouchEvent, pGameLib + 0x675DE4);
        HOOK(Render2DStuff, aml->GetSym(pGameHandle, "_Z13Render2dStuffv"));
        HOOK(IsTouched, pGameLib + 0x2B0CBC + 1);
        SET_TO(m_bMenuOpened, pGameLib + 0x6E0098);
        SET_TO(m_UserPause, aml->GetSym(pGameHandle, "_ZN6CTimer11m_UserPauseE"));
    }
}
