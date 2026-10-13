/* minic_runtime.c — MiniC runtime support library.
 *
 * Compiled alongside the generated IR by the minicc driver.
 * Only one function for now: minic_print, which the codegen calls for
 * every `print(expr)` statement.
 *
 * WHY A SEPARATE RUNTIME?
 *   We could call printf directly from the IR by declaring it as a vararg
 *   extern and emitting a GEP to a format-string global. That works but adds
 *   ~10 lines of fiddly IR-builder code for a non-educational reason.
 *   Putting it here keeps Codegen.cpp focused on the language semantics.
 *
 * SIMPLIFICATION 🏷️: A real language runtime (e.g., libc++) also provides
 *   stack-unwinding tables, exception personality routines, global constructors,
 *   and thread-local storage helpers. Ours is just one function.
 */
#include <stdio.h>

void minic_print(long long val) {
    printf("%lld\n", val);
}
