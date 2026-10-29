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

#include "plic.h"
#include "soc.h"
#include "los_reg.h"
#include "los_arch_interrupt.h"
#include "los_hwi_pri.h"
#include "los_interrupt.h"
#include "los_debug.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

STATIC VOID OsMachineExternalInterrupt(VOID *arg)
{
    volatile UINT32 *plicReg = (volatile UINT32 *)(PLIC_REG_BASE + 0x4);
    UINT32 irqNum, saveIrqNum;

    READ_UINT32(irqNum, plicReg);
    saveIrqNum = irqNum;

    if ((irqNum >= OS_RISCV_CUSTOM_IRQ_VECTOR_CNT) || (irqNum == 0)) {
        HalHwiDefaultHandler((VOID *)irqNum);
    }

    irqNum += RISCV_SYS_MAX_IRQ;

    OsIntHandle(irqNum, &g_hwiHandleForm[irqNum]);

    WRITE_UINT32(saveIrqNum, plicReg);
}

#define OS_PLIC_MAX ((OS_RISCV_CUSTOM_IRQ_VECTOR_CNT >> 5) + 1)

VOID PlicIrqInit(VOID)
{
    volatile UINT32 *plicPrioReg = (volatile UINT32 *)PLIC_PRIO_BASE;
    volatile UINT32 *plicEnReg = (volatile UINT32 *)PLIC_ENABLE_BASE;
    volatile UINT32 *plicReg = (volatile UINT32 *)PLIC_REG_BASE;
    INT32 i;
    UINT32 ret;

    for (i = 0; i < OS_PLIC_MAX; i++) {
        WRITE_UINT32(0x0, plicEnReg);
        plicEnReg++;
    }

    for (i = 0; i < OS_RISCV_CUSTOM_IRQ_VECTOR_CNT; i++) {
        WRITE_UINT32(0x0, plicPrioReg);
        plicPrioReg++;
    }

    WRITE_UINT32(0, plicReg);

    ret = LOS_HwiCreate(RISCV_MACH_EXT_IRQ, 0x1, 0, OsMachineExternalInterrupt, 0);
    if (ret != LOS_OK) {
        PRINT_ERR("Create machine external failed! ret : 0x%x\n", ret);
    }
}

VOID PlicIrqSetPrio(UINT32 vector, UINT32 pri)
{
    volatile UINT32 *plicReg = (volatile UINT32 *)PLIC_PRIO_BASE;

    plicReg += (vector - RISCV_SYS_MAX_IRQ);
    WRITE_UINT32(pri, plicReg);
}

VOID PlicIrqEnable(UINT32 vector)
{
    UINT32 irqValue;
    UINT32 locIrq = vector - RISCV_SYS_MAX_IRQ;
    volatile UINT32 *plicReg = (volatile UINT32 *)PLIC_ENABLE_BASE;

    plicReg += (locIrq >> 5); /* 5： The PLIC interrupt controls the bit width */
    READ_UINT32(irqValue, plicReg);
    irqValue |= (1 << (locIrq & 31)); /* 31: plic irq mask */
    WRITE_UINT32(irqValue, plicReg);
}

VOID PlicIrqDisable(UINT32 vector)
{
    UINT32 irqValue;
    UINT32 locIrq = vector - RISCV_SYS_MAX_IRQ;
    volatile UINT32 *plicReg = (volatile UINT32 *)PLIC_ENABLE_BASE;

    plicReg += (locIrq >> 5); /* 5： The PLIC interrupt controls the bit width */
    READ_UINT32(irqValue, plicReg);
    irqValue &= ~(1 << (locIrq & 31)); /* 31: plic irq mask */
    WRITE_UINT32(irqValue, plicReg);
}

/* ---- HwiControllerOps (new riscv interrupt framework) ----
 * The qemu virt PLIC manages external device IRQs (hwiNum > RISCV_SYS_MAX_IRQ).
 * Local/CPU interrupts (hwiNum <= RISCV_SYS_MAX_IRQ: timer=7, ext pin=11,
 * software=3) are enabled/disabled via the mie CSR, not the PLIC. The ops
 * below dispatch accordingly — passing a local IRQ number to the PLIC register
 * math would underflow and fault (vector - RISCV_SYS_MAX_IRQ goes huge). */

STATIC UINT32 PlicOpsClearIrq(HWI_HANDLE_T hwiNum)
{
    if (hwiNum <= RISCV_SYS_MAX_IRQ) {
        return LOS_OK; /* local interrupt, not PLIC-managed */
    }
    volatile UINT32 *plicReg = (volatile UINT32 *)(PLIC_REG_BASE + 0x4);
    UINT32 irqNum = hwiNum - RISCV_SYS_MAX_IRQ;
    WRITE_UINT32(irqNum, plicReg); /* complete the PLIC claim */
    return LOS_OK;
}

STATIC UINT32 PlicOpsEnableIrq(HWI_HANDLE_T hwiNum)
{
    if (hwiNum <= RISCV_SYS_MAX_IRQ) {
        SET_CSR(mie, 1 << hwiNum); /* local interrupt: enable via mie CSR */
        return LOS_OK;
    }
    PlicIrqEnable(hwiNum);
    return LOS_OK;
}

STATIC UINT32 PlicOpsDisableIrq(HWI_HANDLE_T hwiNum)
{
    if (hwiNum <= RISCV_SYS_MAX_IRQ) {
        CLEAR_CSR(mie, 1 << hwiNum);
        return LOS_OK;
    }
    PlicIrqDisable(hwiNum);
    return LOS_OK;
}

STATIC UINT32 PlicOpsSetPrio(HWI_HANDLE_T hwiNum, HWI_PRIOR_T priority)
{
    if (hwiNum <= RISCV_SYS_MAX_IRQ) {
        return LOS_OK; /* local interrupt, no PLIC priority */
    }
    PlicIrqSetPrio(hwiNum, (UINT32)priority);
    return LOS_OK;
}

STATIC UINT32 PlicOpsGetCurIrq(VOID)
{
    volatile UINT32 *plicReg = (volatile UINT32 *)(PLIC_REG_BASE + 0x4);
    UINT32 irqNum;
    READ_UINT32(irqNum, plicReg);
    return irqNum + RISCV_SYS_MAX_IRQ;
}

STATIC HwiHandleInfo *PlicOpsGetHandleForm(HWI_HANDLE_T hwiNum)
{
    return &g_hwiHandleForm[hwiNum];
}

STATIC HwiControllerOps g_hwiControllerOps = {
    .clearIrq       = PlicOpsClearIrq,
    .enableIrq      = PlicOpsEnableIrq,
    .disableIrq     = PlicOpsDisableIrq,
    .setIrqPriority = PlicOpsSetPrio,
    .getCurIrqNum   = PlicOpsGetCurIrq,
    .getHandleForm  = PlicOpsGetHandleForm,
};

HwiControllerOps *HwiControllerOpsGet(VOID)
{
    return &g_hwiControllerOps;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

