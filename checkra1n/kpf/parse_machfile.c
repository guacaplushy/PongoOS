#include "kpf.h"
#include <paleinfo.h>
#include <pongo.h>
#include <xnu/xnu.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool found_static_binaries = false;
static bool static_binaries_callback(struct xnu_pf_patch* patch, uint32_t* opcode_stream) {
    if (found_static_binaries) {
        panic("KPF: parse_machfile: Found twice!");
    }
    uint32_t* bne = find_next_insn(opcode_stream+4, 3, 0x54000001, 0xff00001f); // b.ne
    if (!bne) return false;
    *bne = NOP;
    puts("KPF: Found parse_machfile");
    found_static_binaries = true;
    return true;
}

// The older style is more complicated
static bool static_binaries_old_callback(struct xnu_pf_patch* patch, uint32_t* opcode_stream) {
    DEVLOG("found parse_machfile candidate @ 0x%llx", xnu_ptr_to_va(opcode_stream));
    if (found_static_binaries) {
        panic("KPF: parse_machfile: Found twice!");
    }
    // Step 1: Find the check for MH_DYLINKER
    uint32_t* cmp = find_prev_insn(opcode_stream, 0x10, 0x71001c1f, 0xfffffc1f); // cmp wN, #0x7
    if ((cmp[1] & 0xff00001f) != 0x54000000) return false; /* b.eq */
    uint32_t* dylinker_stream = &cmp[1] + sxt32(cmp[1] >> 5, 19);

    DEVLOG("parse_machfile: Found MH_DYLINKER check @ 0x%llx", xnu_ptr_to_va(dylinker_stream));

    // Step 2: Make sure this is the depth == 2 check
    if (
        (dylinker_stream[0] & 0xfffffc1f) != 0x7100081f || /* cmp wN, #0x2 */
        (dylinker_stream[1] & 0xff00001f) != 0x54000001    /* b.ne */
    ) return false;

    DEVLOG("parse_machfile: Found depth==2 check");

    // Step 3: We skip over the dylinker-specific code path and find the call to common code
    uint32_t* common_stream = find_next_insn(&dylinker_stream[3], 3, 0xb4000000, 0xff000000); // cbz

    // Step 4: Actually patch the check for static binary to point to the common code
    if ((opcode_stream[0] & 0xfff80010) == 0x36100000) { // tbz
        DEVLOG("tbz old: 0x%x", opcode_stream[0]);
        *opcode_stream = (opcode_stream[0] & 0xfff8001f) | (((common_stream - opcode_stream) & 0x3fff) << 5); // uint32 takes care of >> 2
        DEVLOG("tbz new: 0x%x", opcode_stream[0]);
    } else if ((opcode_stream[0] & 0xffffffe0) == 0x52800080) { // mov
        uint32_t* load_failure = &opcode_stream[3] + sxt32(opcode_stream[3] >> 5, 19);
        uint8_t wM = (opcode_stream[2] >> 16) & 0x1f;
        // Allow (header->flags & MH_DYLDLINK) == 0
        opcode_stream[2] = 0x36100000 | ((common_stream - &opcode_stream[2]) & 0x3fff) << 5 | wM; /* tbz wM, #0x2, common_stream */
        // Disallow (header->flags & MH_PIE) == 0 in dynamic executables
        opcode_stream[3] = 0x36a80000 | ((load_failure - &opcode_stream[3]) & 0x3fff) << 5 | wM;  /* tbz wM, #0x15, load_failure */
        DEVLOG("static_binaries_old_alt_callback: new checks: 0x%x, 0x%x", opcode_stream[2], opcode_stream[3]);
    } else panic("static_binaries_old_callback: unreachable");

    puts("KPF: Found parse_machfile");
    found_static_binaries = true;
    return true;
}

static void kpf_static_binaries_patch(xnu_pf_patchset_t* patchset) {
    // Match the part where it allows x86_64 static binaries
    uint64_t matches[] = {
        0x37100008, // tbnz w8, #0x2, ...
        0x528000e8, // mov w8, #0x7
        0x72a02008, // mov w8, 0x100, lsl 16
        0x6b08001f, // cmp wN, w8
    };

    uint64_t masks[] = {
        0xfff8001f,
        0xffffffff,
        0xfffffe1f,
        0xfffffc1f
    };

    xnu_pf_maskmatch(patchset, "parse_machfile", matches, masks, sizeof(matches)/sizeof(uint64_t), false, (void*)static_binaries_callback);

    // Match checks for MH_DYLINKER & MH_PIE
    // Case 1: tb(n)z for control flow
    uint64_t matches_old[] = {
        0x36100000, // tbz w{0-15}, #0x2, ...
        0x37a80000, // tbnz w{0-15}, #0x15, ...
        0x52800180, // mov w{0-15}, #0xc
        0x72a02000  // movk w{0-15}, #0x100, lsl 16
    };

    uint64_t masks_old[] = {
        0xfff80010,
        0xfff80010,
        0xfffffff0,
        0xfffffff0
    };
    xnu_pf_maskmatch(patchset, "parse_machfile", matches_old, masks_old, sizeof(matches_old)/sizeof(uint64_t), false, (void*)static_binaries_old_callback);

    // Case 2: bics and b.ne for control flow
    uint64_t matches_old_alt[] = {
        0x52800080, // mov wN, #0x4
        0x72a00400, // mov wN, #0x20, lsl 16
        0x6a20001f, // bics wzr, wN, wM
        0x54000001  // b.ne
    };

    uint64_t masks_old_alt[] = {
        0xffffffe0,
        0xffffffe0,
        0xffe0fc1f,
        0xff00001f
    };
    xnu_pf_maskmatch(patchset, "parse_machfile", matches_old_alt, masks_old_alt, sizeof(matches_old_alt)/sizeof(uint64_t), false, (void*)static_binaries_old_callback);
}

static void kpf_parse_machfile_finish(struct mach_header_64 *hdr, palerain_option_t *palera1n_flags)
{
    if (!found_static_binaries) {
        panic("Missing ptch: parse_machfile");
    }
}

kpf_component_t kpf_parse_machfile =
{
    .finish = kpf_parse_machfile_finish,
    .patches =
    {
        { NULL, "__TEXT_EXEC", "__text", XNU_PF_ACCESS_32BIT, kpf_static_binaries_patch },
        {},
    },
};
