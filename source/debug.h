#ifndef DEBUG_H
#define DEBUG_H

#include "api/m64p_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void DebugMessage(int level, const char *message, ...);

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_H */
