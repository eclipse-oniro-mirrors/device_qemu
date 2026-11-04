/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include "los_task.h"

/* hello task: print a few times then stop, so shell I/O is clean for testing.
 * Set HELLO_LOOP_FOREVER to 1 for the original infinite loop. */
#define HELLO_TASK_STACK_SIZE   0x800
#define HELLO_TASK_PRIO         5
#define HELLO_LOOP_FOREVER      0
#define HELLO_PRINT_TIMES       3
#define HELLO_PRINT_INTERVAL    200  /* ticks (2s @ 100Hz) */

static VOID HelloTaskEntry(VOID)
{
    int i;
    for (i = 0; i < HELLO_PRINT_TIMES; i++) {
        printf("hello word\n");
        (VOID)LOS_TaskDelay(HELLO_PRINT_INTERVAL);
    }
#if HELLO_LOOP_FOREVER
    while (1) {
        printf("hello word\n");
        (VOID)LOS_TaskDelay(HELLO_PRINT_INTERVAL);
    }
#endif
}

unsigned int LosAppInit(void)
{
    UINT32 taskId;
    TSK_INIT_PARAM_S taskParam = {0};

    printf("\n====================\n");
    printf("hello word\n");
    printf("====================\n\n");

    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)HelloTaskEntry;
    taskParam.uwStackSize  = HELLO_TASK_STACK_SIZE;
    taskParam.pcName       = "HelloTask";
    taskParam.usTaskPrio   = HELLO_TASK_PRIO;

    return LOS_TaskCreate(&taskId, &taskParam);
}
