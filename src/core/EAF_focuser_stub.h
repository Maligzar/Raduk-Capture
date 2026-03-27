#pragma once
// ============================================================
// EAF_focuser_stub.h — Minimal EAF SDK stubs for RADUK_EMULATOR builds
//
// Defines the types and no-op functions needed to compile
// EAFFocuser.h and EAFEmulator.h without the real ZWO EAF SDK.
// Only included when RADUK_EMULATOR is defined.
// ============================================================

#include <cstdint>

typedef int EAF_BOOL;
static const EAF_BOOL EAF_FALSE = 0;
static const EAF_BOOL EAF_TRUE  = 1;

typedef enum _EAF_ERROR_CODE {
    EAF_SUCCESS             = 0,
    EAF_ERROR_INVALID_INDEX = 1,
    EAF_ERROR_INVALID_ID    = 2,
    EAF_ERROR_INVALID_VALUE = 3,
    EAF_ERROR_REMOVED       = 4,
    EAF_ERROR_MOVING        = 5,
    EAF_ERROR_ERROR_STATE   = 6,
    EAF_ERROR_GENERAL_ERROR = 7,
    EAF_ERROR_NOT_SUPPORTED = 8,
    EAF_ERROR_CLOSED        = 9,
    EAF_ERROR_END           = 10,
} EAF_ERROR_CODE;

typedef struct _EAF_INFO {
    int     ID;
    char    Name[64];
    int     MaxStep;
    EAF_BOOL Bidirection;
} EAF_INFO;

// ── Stub SDK functions ────────────────────────────────────────
#ifdef RADUK_EMULATOR
inline int            EAFGetNum()                         { return 0; }
inline EAF_ERROR_CODE EAFGetID(int, int*)                 { return EAF_SUCCESS; }
inline EAF_ERROR_CODE EAFOpen(int)                        { return EAF_SUCCESS; }
inline EAF_ERROR_CODE EAFClose(int)                       { return EAF_SUCCESS; }
inline EAF_ERROR_CODE EAFGetProperty(int, EAF_INFO*)      { return EAF_SUCCESS; }
inline EAF_ERROR_CODE EAFMove(int, int)                   { return EAF_SUCCESS; }
inline EAF_ERROR_CODE EAFGetStep(int, int*)               { return EAF_SUCCESS; }
inline EAF_ERROR_CODE EAFIsMoving(int, EAF_BOOL*, EAF_BOOL*) { return EAF_SUCCESS; }
inline EAF_ERROR_CODE EAFResetDeviceHWModel(int)          { return EAF_SUCCESS; }
inline EAF_ERROR_CODE EAFGetTemp(int, float*)             { return EAF_SUCCESS; }
#endif
