/* pemu - AArch64 Linux guest user-mode emulator (entry file; see the included parts). */
/* Layout: target/ = guest CPU, tcg/ = JIT back end, linux-user/ = Linux syscall/signal/ELF layer. */
/* The parts are included in dependency order and form a single translation unit. */
#ifndef PEMU_RELEASE
#define PEMU_RELEASE "dev"   /* build.sh sets it from the VERSION file */
#endif
#include "target/aarch64/cpu.inc.h"
#include "target/aarch64/helpers.inc.h"
#include "linux-user/aarch64/host_syscall.inc.h"
#include "target/aarch64/mem.inc.h"
#include "linux-user/aarch64/signal.inc.h"
#include "linux-user/aarch64/syscall.inc.h"
#include "target/aarch64/fp_simd.inc.h"
#include "target/aarch64/load_store.inc.h"
#include "target/aarch64/step.inc.h"
#include "tcg/x86_64/tcg_target.inc.h"
#include "tcg/x86_64/emit.inc.h"
#include "tcg/x86_64/guest_state.inc.h"
#include "tcg/x86_64/block_cache.inc.h"
#include "tcg/x86_64/helpers.inc.h"
#include "tcg/x86_64/ldst.inc.h"
#include "tcg/x86_64/dataproc.inc.h"
#include "tcg/x86_64/translate.inc.h"
#include "linux-user/aarch64/cpu_loop.inc.h"
#include "linux-user/aarch64/elfload.inc.h"
#include "linux-user/aarch64/main.inc.h"
#include "tests/embed_hook.inc.h"
