/*
 * pongoOS - https://checkra.in
 *
 * Copyright (C) 2023 checkra1n team
 *
 * This file is part of pongoOS.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include "kpf.h"
#include <pongo.h>
#include <xnu/xnu.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool need_developer_mode_patch = false;

static bool found_developer_mode = false;

static bool kpf_developer_mode_callback(struct xnu_pf_patch *patch, uint32_t *opcode_stream) {
    if (found_developer_mode) {
        panic("kpf_developer_mode: Found twice!");
    }
    found_developer_mode = true;
    opcode_stream[5] = 0x14000000 | ((&opcode_stream[0] - &opcode_stream[5]) & 0x03ffffff); // uint32 takes care of >> 2

    puts("KPF: Found developer mode");
    return true;
}

static void kpf_developer_mode_patch(xnu_pf_patchset_t *xnu_text_exec_patchset)
{
    // Force developer mode on.
    // Find enable_developer_mode and disable_developer_mode in AMFI,
    // then we patch the latter to branch to the former
    //
    // Example from iPad 6th gen iOS 16.0 beta 3:
    //
    // ;-- _enable_developer_mode:
    // 0xfffffff007633f74      681300f0       adrp x8, 0xfffffff0078a2000
    // 0xfffffff007633f78      08810291       add x8, x8, 0xa0
    // 0xfffffff007633f7c      29008052       movz w9, 0x1
    // 0xfffffff007633f80      09fd9f08       stlrb w9, [x8]
    // 0xfffffff007633f84      c0035fd6       ret
    // ;-- _disable_developer_mode:
    // 0xfffffff007633f88      681300f0       adrp x8, 0xfffffff0078a2000
    // 0xfffffff007633f8c      08810291       add x8, x8, 0xa0
    // 0xfffffff007633f90      1ffd9f08       stlrb wzr, [x8]
    // 0xfffffff007633f94      c0035fd6       ret
    //
    // /x 08000090080000002900805209010008c0035fd608000090080000001f010008c0035fd6:1f00009fff000000ffffffffff03600effffffff1f00009fff000000ff03600effffffff

    uint64_t matches[] = 
    {
        0x90000008, // adrp x8, *
        0x00000008, // {ldr,add} x8, [x8, #*]
        0x52800029, // mov w9, #0x1
        0x08000109, // str{l,}b w9, [x8]
        0xd65f03c0, // ret
        0x90000008, // adrp x8, *
        0x00000008, // {ldr,add} x8, [x8, #*]
        0x0800011f, // str{l,}b wzr, [x8]
        0xd65f03c0, // ret
    };
    
    uint64_t masks[] = {
        0x9f00001f,
        0x000000ff,
        0xffffffff,
        0x0e6003ff,
        0xffffffff,
        0x9f00001f,
        0x000000ff,
        0x0e6003ff,
        0xffffffff,
    };
    xnu_pf_maskmatch(xnu_text_exec_patchset, "developer_mode", matches, masks, sizeof(matches)/sizeof(uint64_t), true, (void*)kpf_developer_mode_callback);

}

static void kpf_developer_mode_init(struct mach_header_64 *hdr, xnu_pf_range_t *cstring, palerain_option_t palera1n_flags)
{
    struct mach_header_64 *amfi = xnu_pf_get_kext_header(hdr, "com.apple.driver.AppleMobileFileIntegrity");
    xnu_pf_range_t *amfi_cstring = xnu_pf_section(amfi, "__TEXT", "__cstring");
    xnu_pf_range_t *range = amfi_cstring ? amfi_cstring : cstring;

    const char dev_mode_string[] = "AMFI: developer mode is force enabled\n";
    const char *dev_mode_string_match = memmem(range->cacheable_base, range->size, dev_mode_string, sizeof(dev_mode_string));

    if(amfi_cstring)
    {
        free(amfi_cstring);
    }

#ifdef DEV_BUILD
    // 16.0 beta 1 onwards
    if((dev_mode_string_match != NULL) != (gKernelVersion.darwinMajor >= 22) && xnu_platform() == PLATFORM_IOS)
    {
        panic("Developer mode doesn't match expected XNU version");
    }
#endif

    need_developer_mode_patch = dev_mode_string_match != NULL;
}

static void kpf_developer_mode_patches(xnu_pf_patchset_t *xnu_text_exec_patchset)
{
    if(need_developer_mode_patch) // iOS 16+ only
    {
        kpf_developer_mode_patch(xnu_text_exec_patchset);
    }
}

kpf_component_t kpf_developer_mode =
{
    .init = kpf_developer_mode_init,
    .patches =
    {
        { NULL, "__TEXT_EXEC", "__text", XNU_PF_ACCESS_32BIT, kpf_developer_mode_patches },
        {},
    },
};
