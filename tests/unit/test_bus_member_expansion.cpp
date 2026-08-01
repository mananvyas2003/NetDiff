// T0.4-d: deliberate FAILING test — bus member expansion not implemented.
//
// TODO: Do not invent KiCad bus `{A B}` / `[0..N]` expansion rules until answered:
// 1. For names like "RS485{RS485}" or "DIS_USB[0..3]", where is the member list defined —
//    inline in the name, a separate (members ...) node, or only via bus_entry → member nets?
// 2. Does `{A B C}` expand to nets A,B,C (with optional prefix left of `{`), or is the whole
//    token the bus name with members discovered elsewhere?
// 3. For `[0..N]` / `[N..0]`, how are basename, zero-padding, and direction defined?
// 4. How does bus_entry join a member wire to a bus net without member expansion?

#include <iostream>

int main() {
    std::cerr
        << "FAIL (expected): bus member expansion not implemented — see TODO in "
           "tests/unit/test_bus_member_expansion.cpp\n";
    return 1;
}
