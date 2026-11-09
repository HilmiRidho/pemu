/* pemu_x64 - x86_64 Linux guest user-mode emulator (entry file; see the included parts). */
/* Layout modelled on qemu-user: target/ = guest CPU, tcg/ = JIT back end, linux-user/ = Linux syscall/signal/ELF layer. */
/* The parts are included in dependency order and form a single translation unit. */
#ifndef PEMU_RELEASE
#define PEMU_RELEASE "dev"   /* build.sh sets it from the VERSION file */
#endif
#include "target/x86_64/cpu.inc.h"
#include "linux-user/x86_64/host_syscall.inc.h"
#include "target/x86_64/flags_mem.inc.h"
#include "linux-user/x86_64/signal.inc.h"
#include "linux-user/x86_64/syscall.inc.h"
#include "target/x86_64/decode.inc.h"
#include "target/x86_64/sse2.inc.h"
#include "target/x86_64/mmx.inc.h"
#include "target/x86_64/ssse3_sse4.inc.h"
#include "target/x86_64/x87.inc.h"
#include "target/x86_64/interp.inc.h"
#ifdef HAVE_JIT
#include "tcg/aarch64/tcg_target.inc.h"
#include "tcg/aarch64/emit.inc.h"
#include "tcg/aarch64/guest_state.inc.h"
#include "tcg/aarch64/block_cache.inc.h"
#include "tcg/aarch64/decode.inc.h"
#include "tcg/aarch64/codegen.inc.h"
#include "tcg/aarch64/translate.inc.h"
#endif /* HAVE_JIT */
#include "linux-user/x86_64/cpu_loop.inc.h"
#include "linux-user/x86_64/elfload.inc.h"
#include "linux-user/x86_64/main.inc.h"
