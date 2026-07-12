#pragma once
// ============================================================
// ASICamera2Stub.h — Minimal ASI SDK type stubs for RADUK_EMULATOR builds
//
// Defines just enough of the ASICamera2 API surface to compile
// ASICamera.h, CameraEmulator.h, and CaptureThread without the
// real ZWO SDK headers present.
//
// Only included when RADUK_EMULATOR is defined.
// The real SDK header (ASICamera2.h) is used in hardware builds.
// ============================================================

#include <cstring>
#include <cstdint>

// ── Boolean type ─────────────────────────────────────────────
typedef int ASI_BOOL;
static const ASI_BOOL ASI_FALSE = 0;
static const ASI_BOOL ASI_TRUE  = 1;

// ── Image types ───────────────────────────────────────────────
typedef enum _ASI_IMG_TYPE {
    ASI_IMG_RAW8  = 0,
    ASI_IMG_RGB24 = 1,
    ASI_IMG_RAW16 = 2,
    ASI_IMG_Y8    = 3,
    ASI_IMG_END   = -1
} ASI_IMG_TYPE;

// ── Bayer pattern ─────────────────────────────────────────────
typedef enum _ASI_BAYER_PATTERN {
    ASI_BAYER_RG = 0,
    ASI_BAYER_BG = 1,
    ASI_BAYER_GR = 2,
    ASI_BAYER_GB = 3
} ASI_BAYER_PATTERN;

// ── Flip status ───────────────────────────────────────────────
typedef enum _ASI_FLIP_STATUS {
    ASI_FLIP_NONE  = 0,
    ASI_FLIP_HORIZ = 1,
    ASI_FLIP_VERT  = 2,
    ASI_FLIP_BOTH  = 3
} ASI_FLIP_STATUS;

// ── Control types ─────────────────────────────────────────────
typedef enum _ASI_CONTROL_TYPE {
    ASI_GAIN              = 0,
    ASI_EXPOSURE          = 1,
    ASI_GAMMA             = 2,
    ASI_WB_R              = 3,
    ASI_WB_B              = 4,
    ASI_OFFSET            = 5,
    ASI_BANDWIDTHOVERLOAD = 6,
    ASI_OVERCLOCK         = 7,
    ASI_TEMPERATURE       = 8,
    ASI_FLIP              = 9,
    ASI_AUTO_MAX_GAIN     = 10,
    ASI_AUTO_MAX_EXP      = 11,
    ASI_AUTO_TARGET_BRIGHTNESS = 12,
    ASI_HARDWARE_BIN      = 13,
    ASI_HIGH_SPEED_MODE   = 14,
    ASI_COOLER_POWER_PERC = 15,
    ASI_TARGET_TEMP       = 16,
    ASI_COOLER_ON         = 17,
    ASI_MONO_BIN          = 18,
    ASI_FAN_ON            = 19,
    ASI_PATTERN_ADJUST    = 20,
    ASI_ANTI_DEW_HEATER   = 21,
    ASI_HUMIDITY          = 22,
    ASI_ENABLE_DDR        = 23,
    ASI_FPS_LIMIT         = 24
} ASI_CONTROL_TYPE;

// ── Error codes ───────────────────────────────────────────────
typedef enum _ASI_ERROR_CODE {
    ASI_SUCCESS                = 0,
    ASI_ERROR_INVALID_INDEX    = 1,
    ASI_ERROR_INVALID_ID       = 2,
    ASI_ERROR_INVALID_CONTROL_TYPE = 3,
    ASI_ERROR_CAMERA_CLOSED    = 4,
    ASI_ERROR_CAMERA_REMOVED   = 5,
    ASI_ERROR_INVALID_PATH     = 6,
    ASI_ERROR_INVALID_FILEFORMAT = 7,
    ASI_ERROR_INVALID_SIZE     = 8,
    ASI_ERROR_INVALID_IMGTYPE  = 9,
    ASI_ERROR_OUTOF_BOUNDARY   = 10,
    ASI_ERROR_TIMEOUT          = 11,
    ASI_ERROR_INVALID_SEQUENCE = 12,
    ASI_ERROR_BUFFER_TOO_SMALL = 13,
    ASI_ERROR_VIDEO_MODE_ACTIVE = 14,
    ASI_ERROR_EXPOSURE_IN_PROGRESS = 15,
    ASI_ERROR_GENERAL_ERROR    = 16,
    ASI_ERROR_INVALID_MODE     = 17,
    ASI_ERROR_END              = 18
} ASI_ERROR_CODE;

// ── Camera info struct ────────────────────────────────────────
typedef struct _ASI_CAMERA_INFO {
    char          Name[64];
    int           CameraID;
    long          MaxHeight;
    long          MaxWidth;
    ASI_BOOL      IsColor;
    ASI_BAYER_PATTERN BayerPattern;
    int           SupportedBins[16];
    ASI_IMG_TYPE  SupportedVideoFormat[8];
    double        PixelSize;
    ASI_BOOL      MechanicalShutter;
    ASI_BOOL      ST4Port;
    ASI_BOOL      IsCoolerCam;
    ASI_BOOL      IsUSB3Host;
    ASI_BOOL      IsUSB3Camera;
    float         ElecPerADU;
    int           BitDepth;
    ASI_BOOL      IsTriggerCam;
    char          Unused[16];
} ASI_CAMERA_INFO;

// ── Control caps struct ───────────────────────────────────────
typedef struct _ASI_CONTROL_CAPS {
    char             Name[64];
    char             Description[128];
    long             MaxValue;
    long             MinValue;
    long             DefaultValue;
    ASI_BOOL         IsAutoSupported;
    ASI_BOOL         IsWritable;
    ASI_CONTROL_TYPE ControlType;
    char             Unused[32];
} ASI_CONTROL_CAPS;

// ── Stub SDK functions (no-op implementations) ────────────────
// Only used in emulator builds. In hardware builds, the real
// SDK provides these functions via ASICamera2.lib.
#ifdef RADUK_EMULATOR
inline int ASIGetNumOfConnectedCameras() { return 0; }
inline ASI_ERROR_CODE ASIGetCameraProperty(ASI_CAMERA_INFO*, int) { return ASI_SUCCESS; }
#endif
