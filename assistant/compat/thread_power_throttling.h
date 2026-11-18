/* What llama.cpp's CPU backend needs from the Windows SDK's processthreadsapi.h that MinGW-w64's older headers lack
 * (Ubuntu 24.04's): the thread power throttling state, which keeps Windows 11 from parking the cores the threads run on.
 * Forced into its sources only where the headers lack it (CMakeLists.txt checks); the types C has, no windows.h. */
#ifndef FCG_THREAD_POWER_THROTTLING_H
#define FCG_THREAD_POWER_THROTTLING_H

#define THREAD_POWER_THROTTLING_CURRENT_VERSION 1
#define THREAD_POWER_THROTTLING_EXECUTION_SPEED 0x1
typedef struct _THREAD_POWER_THROTTLING_STATE {
    unsigned long Version;
    unsigned long ControlMask;
    unsigned long StateMask;
} THREAD_POWER_THROTTLING_STATE;

#endif
