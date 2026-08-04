/* C++20 modules basic syntax parsing test.
 *
 * This tests that the parser accepts the basic C++20 module,
 * import, and export syntax.  Since mcc does not yet implement
 * semantic module loading, this test only verifies parsing.
 * Returns 0 on success (compilation + runtime). */

/* Module implementation unit */
module BasicModule;

/* Import a module */
import Something;

/* Import with dotted name */
import Outer.Module;

/* Export declaration */
export int exported_func(void) { return 42; }

/* Export block */
export {
    int block_func(void) { return 7; }
}

/* Export import */
export import ReExported;

/* Module interface declaration */
export module InterfaceModule;

/* Module :private fragment */
module :private;

/* Nested export inside export block */
export {
    export int nested_export(void) { return 1; }
}

int main(void) {
    /* Verify the exported function works */
    if (exported_func() != 42) return 1;
    if (block_func() != 7) return 2;
    if (nested_export() != 1) return 3;
    return 0;
}