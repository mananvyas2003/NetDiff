// T0.4-d: KiCad bus member expansion (vector + group).
// Rules from Schematic Editor docs / SCH_CONNECTION — not invented.

#include "netdiff/bus.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using netdiff::ExpandBusMembers;
using netdiff::IsBusName;

static bool SameSet(std::vector<std::string> a, std::vector<std::string> b) {
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return a == b;
}

static void Expect(const std::string& name, const std::vector<std::string>& want) {
    const auto got = ExpandBusMembers(name);
    if (!SameSet(got, want)) {
        std::cerr << "ExpandBusMembers(\"" << name << "\") mismatch\n  got:";
        for (const auto& g : got) {
            std::cerr << " [" << g << "]";
        }
        std::cerr << "\n  want:";
        for (const auto& w : want) {
            std::cerr << " [" << w << "]";
        }
        std::cerr << "\n";
        std::exit(1);
    }
}

int main() {
    if (IsBusName("AN0") || !IsBusName("AN[0..7]") || !IsBusName("USB1{DP DM}")) {
        std::cerr << "IsBusName failed\n";
        return 1;
    }

    Expect("AN0", {"AN0"});
    Expect("DATA[0..7]", {"DATA0", "DATA1", "DATA2", "DATA3", "DATA4", "DATA5", "DATA6", "DATA7"});
    Expect("A[3..0]", {"A0", "A1", "A2", "A3"});
    Expect("IRQ-[1..7]", {"IRQ-1", "IRQ-2", "IRQ-3", "IRQ-4", "IRQ-5", "IRQ-6", "IRQ-7"});
    Expect("DTIN[0..3]", {"DTIN0", "DTIN1", "DTIN2", "DTIN3"});
    Expect("DATA[00..03]", {"DATA00", "DATA01", "DATA02", "DATA03"});

    Expect("{SCL SDA}", {"SCL", "SDA"});
    Expect("USB1{DP DM}", {"USB1.DP", "USB1.DM"});
    Expect("SPI{SCK, MOSI, MISO}", {"SPI.SCK", "SPI.MOSI", "SPI.MISO"});
    Expect("I2C{SCL, SDA}", {"I2C.SCL", "I2C.SDA"});
    Expect("MEMORY{A[1..0] D0}", {"MEMORY.A0", "MEMORY.A1", "MEMORY.D0"});

    // Alias: USB3-1{USB3} with USB3 → TX+/TX-/…
    {
        netdiff::BusAliasMap aliases;
        aliases["USB3"] = {"TX+", "TX-", "RX+", "RX-", "D-", "D+"};
        aliases["SD"] = {"D[0..3]", "CMD", "CLK"};
        aliases["PCIe"] = {"~{RST}", "~{CLKREQ}", "CLK+", "CLK-"};
        auto got = netdiff::ExpandBusMembers("USB3-1{USB3}", aliases);
        std::vector<std::string> want = {"USB3-1.TX+", "USB3-1.TX-", "USB3-1.RX+",
                                         "USB3-1.RX-", "USB3-1.D-", "USB3-1.D+"};
        if (!SameSet(got, want)) {
            std::cerr << "alias USB3-1{USB3} failed\n";
            return 1;
        }
        got = netdiff::ExpandBusMembers("SD{SD}", aliases);
        want = {"SD.D0", "SD.D1", "SD.D2", "SD.D3", "SD.CMD", "SD.CLK"};
        if (!SameSet(got, want)) {
            std::cerr << "alias SD{SD} failed\n";
            return 1;
        }
        got = netdiff::ExpandBusMembers("PCIe{PCIe}", aliases);
        want = {"PCIe.~{RST}", "PCIe.~{CLKREQ}", "PCIe.CLK+", "PCIe.CLK-"};
        if (!SameSet(got, want)) {
            std::cerr << "alias PCIe{PCIe} overbar members failed\n";
            for (const auto& g : got) {
                std::cerr << "  [" << g << "]\n";
            }
            return 1;
        }
        if (IsBusName("~{LED_PCIE}") || IsBusName("~{EN}")) {
            std::cerr << "overbar must not be IsBusName\n";
            return 1;
        }
    }

    std::cout << "bus member expansion: ok\n";
    return 0;
}
