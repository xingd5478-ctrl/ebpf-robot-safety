#ifndef CLI_SHELL_H
#define CLI_SHELL_H

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

void CLI_Init(void);
void Task_CLI(void *param);

#ifdef __cplusplus
}
#endif

#endif /* CLI_SHELL_H */
