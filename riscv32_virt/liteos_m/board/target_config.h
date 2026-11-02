/*
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**@defgroup los_config System configuration items
 * @ingroup kernel
 */

#ifndef _TARGET_CONFIG_H
#define _TARGET_CONFIG_H

#include "stdint.h"
#include "stdbool.h"
#include "soc.h"
#include "los_compiler.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/*=============================================================================
                                        System clock module configuration
=============================================================================*/
#define OS_SYS_CLOCK                                        10000000UL
#define LOSCFG_BASE_CORE_TICK_HW_TIME                       0
#define LOSCFG_BASE_CORE_TICK_WTIMER                        1
#define LOSCFG_BASE_CORE_TICK_RESPONSE_MAX                  ((UINT64)-1)

/*=============================================================================
                                       Task module configuration
=============================================================================*/
#define LOSCFG_BASE_CORE_TIMESLICE                          1
#define LOSCFG_BASE_CORE_TIMESLICE_TIMEOUT                  20000
#define LOSCFG_BASE_CORE_TSK_MONITOR                        1

/*=============================================================================
                                       Semaphore module configuration
=============================================================================*/
#define LOSCFG_BASE_IPC_SEM                                 1
/*=============================================================================
                                       Mutex module configuration
=============================================================================*/
#define LOSCFG_BASE_IPC_MUX                                 1
/*=============================================================================
                                       Queue module configuration
=============================================================================*/
#define LOSCFG_BASE_IPC_QUEUE                               1
/*=============================================================================
                                       Software timer module configuration
=============================================================================*/
#define LOSCFG_BASE_CORE_SWTMR                              1
#define LOSCFG_BASE_CORE_SWTMR_ALIGN                        1
#define LOSCFG_BASE_CORE_SWTMR_LIMIT                        48
/*=============================================================================
                                       Memory module configuration
=============================================================================*/
extern UINTPTR __heap_start;
extern UINTPTR __heap_size;
#define LOSCFG_SYS_EXTERNAL_HEAP                            1
#define LOSCFG_SYS_HEAP_ADDR                                (VOID *)&__heap_start
/* Reserve the last 64KiB of the heap for newlib malloc (_sbrk_r in
 * lib/libc/newlib/porting/src/hook_adapt.c). Without this, LOS_MemInit
 * would register the whole [__heap_start, +__heap_size) as the LiteOS
 * TLSF pool and newlib malloc would fight it for the same tail bytes
 * (corrupting pool metadata -> OsMemPoolHeadCheck fault). */
#define LOSCFG_NEWLIB_HEAP_SIZE                             0x10000
#define LOSCFG_SYS_HEAP_SIZE                                ((UINTPTR)&__heap_size - LOSCFG_NEWLIB_HEAP_SIZE)
#define LOSCFG_BASE_MEM_NODE_SIZE_CHECK                     0
#define LOSCFG_MEM_MUL_POOL                                 1
#define OS_SYS_MEM_NUM                                      20
#define LOSCFG_KERNEL_MEM_SLAB                              0
#define LOSCFG_MEMORY_BESTFIT                               1
/*=============================================================================
                                        Exception module configuration
=============================================================================*/
#define LOSCFG_PLATFORM_HWI_WITH_ARG                        1

#define LOSCFG_KERNEL_PRINTF                                1

#define LOS_KERNEL_TEST_NOT_SMOKE                           0
#define LOS_KERNEL_HWI_TEST                                 0
/*=============================================================================
                                       shell module configuration
=============================================================================*/
#define LOSCFG_USE_SHELL                                    1
#define LOSCFG_SHELL_PRIO                                   3

extern UINT32 QemuCLZ(UINT32);
#undef CLZ
#define CLZ(n) QemuCLZ(n)
#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */


#endif /* _TARGET_CONFIG_H */
