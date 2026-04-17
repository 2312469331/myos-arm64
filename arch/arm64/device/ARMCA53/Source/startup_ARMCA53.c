/******************************************************************************
 * @file     startup_ARMCA53.c
 * @brief    CMSIS Device System Source File for Arm Cortex-A53 Device Series
 * @version  V1.0.0
 * @date     31. March 2024
 ******************************************************************************/
/*
 * Copyright (c) 2009-2021 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ARMCA53.h>

/*----------------------------------------------------------------------------
  Internal References
 *----------------------------------------------------------------------------*/
void Vectors        (void) __attribute__ ((section("RESET")));
void Reset_Handler  (void);
void Default_Handler(void) __attribute__ ((noreturn));

/*----------------------------------------------------------------------------
  Exception / Interrupt Handler
 *----------------------------------------------------------------------------*/
void Undef_Handler (void) __attribute__ ((weak, noreturn, alias("Default_Handler")));
void SVC_Handler   (void) __attribute__ ((weak, noreturn, alias("Default_Handler")));
void PAbt_Handler  (void) __attribute__ ((weak, noreturn, alias("Default_Handler")));
void DAbt_Handler  (void) __attribute__ ((weak, noreturn, alias("Default_Handler")));
void IRQ_Handler   (void) __attribute__ ((weak, noreturn, alias("Default_Handler")));
void FIQ_Handler   (void) __attribute__ ((weak, noreturn, alias("Default_Handler")));

/*----------------------------------------------------------------------------
  Exception / Interrupt Vector Table
 *----------------------------------------------------------------------------*/
__attribute__((section("RESET")))
void Vectors(void) {
  __ASM volatile(
  ".align 3                                      \n"
  "b     Reset_Handler                            \n"
  "b     Undef_Handler                            \n"
  "b     SVC_Handler                              \n"
  "b     PAbt_Handler                             \n"
  "b     DAbt_Handler                             \n"
  "b     .                                        \n"
  "b     IRQ_Handler                              \n"
  "b     FIQ_Handler                              \n"
  );
}

/*----------------------------------------------------------------------------
  Reset Handler called on controller reset
 *----------------------------------------------------------------------------*/
void Reset_Handler(void) {
  __ASM volatile(
  // Call _start (kernel entry point)
  "bl     .                                   \n"
  );
}

/*----------------------------------------------------------------------------
  Default Handler for Exceptions / Interrupts
 *----------------------------------------------------------------------------*/
void Default_Handler(void) {
  while(1);
}
