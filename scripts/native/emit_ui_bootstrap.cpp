#include "../../src/native/native_ui.h"
#include <fstream>
#include <iterator>
#include <iostream>
int main() {
    std::ifstream js("src/ui/loadout_panel.js"), css("src/ui/loadout_panel.css"), icons("src/ui/weapon_icons.js");
    if (!js || !css || !icons) return 1;
    std::ifstream navigation("src/ui/loadout_native_navigation.js");
    if(!navigation) return 1;
    auto script = native_ui::make_owned_bootstrap(
        std::string(std::istreambuf_iterator<char>(navigation), {})+"\n"+std::string(std::istreambuf_iterator<char>(js), {}),
        std::string(std::istreambuf_iterator<char>(css), {}),
        std::string(std::istreambuf_iterator<char>(icons), {}));
    if (script.empty()) return 2;
    std::ifstream transport("src/ui/loadout_transport.js");
    if (!transport) return 3;
    std::cout << script << "\n" << std::string(std::istreambuf_iterator<char>(transport), {});
}
