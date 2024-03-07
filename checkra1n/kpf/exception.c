#include "kpf.h"
#include <pongo.h>
#include <xnu/xnu.h>
#include <stdint.h>
#include <string.h>

static bool need_set_exception_patch = false;
static uint32_t* set_exception_stp = NULL;

static void set_exception_behavior_allowed_init(struct mach_header_64 *hdr, xnu_pf_range_t *cstring, palerain_option_t palera1n_flags) {
    const char set_exception_string[] = "com.apple.private.set-exception-port";
    const char *set_exception_string_match = memmem(cstring->cacheable_base, cstring->size, set_exception_string, sizeof(set_exception_string));

#ifdef DEV_BUILD
    // 17.0 beta 1 onwards
    if((set_exception_string_match != NULL) != (gKernelVersion.darwinMajor >= 23))
    {
        panic("Set exception presence doesn't match expected Darwin version");
    }
#endif

    need_set_exception_patch = (set_exception_string_match != NULL);
}

bool kpf_set_exception_callback(struct xnu_pf_patch *patch, uint32_t *opcode_stream) {
    uint32_t *stp = find_prev_insn(opcode_stream, 0x50, 0xa9007bfd, 0xffc07fff); // stp x29, x30, [sp, ...]
    if(!stp) {
        panic_at(opcode_stream, "kpf_set_exception_callback: Failed to find stack frame");
    }

    if (set_exception_stp && stp != set_exception_stp) {
        panic_at(stp, "kpf_set_exception_callback: Found twice!");
    } else if (set_exception_stp) {
        return true;
    }

    uint32_t *start = find_prev_insn(stp, 10, 0xa98003e0, 0xffc003e0); // stp xN, xM, [sp, ...]!
    if(!start)
    {
        start = find_prev_insn(stp, 10, 0xd10003ff, 0xffc003ff); // sub sp, sp, ...
        if(!start) {
            panic_at(stp, "kpf_set_exception_callback: Failed to find start of function");
        }
    }

    // Allow everything
    start[0] = 0x52800020; // mov w0, #1
    start[1] = RET;
    set_exception_stp = stp;

    puts("KPF: Found set_exception_behavior_allowed");
    return true;
}

static void set_exception_behavior_allowed_init_patch(xnu_pf_patchset_t *patchset) {
    // On iOS 17, setting exception ports now requires an entitlement for platform binaries
    // But we make every binary a platform binary so some app store apps could crash
    // This is a distinctive pattern that appears two times within the function
    uint64_t matches[] = {
        0x12007208, // and w8, w{16-31}, #0x1fffffff
        0x7100111f, // cmp w8, #0x4
        0x54000000, // b.eq
    };

    uint64_t masks[] = {
        0xffdffe1f,
        0xffffffff,
        0xff00001f
    };

    xnu_pf_maskmatch(patchset, "set_exception_behavior_allowed", matches, masks, sizeof(masks)/sizeof(uint64_t), true, (void*)kpf_set_exception_callback);
}

static void set_exception_behavior_allowed_init_patches(xnu_pf_patchset_t *xnu_text_exec_patchset) {
    if (need_set_exception_patch) {
        set_exception_behavior_allowed_init_patch(xnu_text_exec_patchset);
    }
}

kpf_component_t kpf_set_exception_behavior_allowed =
{
    .init = set_exception_behavior_allowed_init,
    .patches =
    {
        { NULL, "__TEXT_EXEC", "__text", XNU_PF_ACCESS_32BIT, set_exception_behavior_allowed_init_patches },
        {},
    },
};
