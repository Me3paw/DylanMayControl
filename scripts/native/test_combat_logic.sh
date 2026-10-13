#!/usr/bin/env bash
# Host-side proof of src/native/combat_mod.h (pure logic) + Windows cross-compile check of the header.
set -euo pipefail
source "$(dirname -- "$0")/../env.sh"
cd "$LAB_ROOT"
mkdir -p .work/native-build
g++ -std=c++20 -O1 -Wall -Wextra -Werror -fno-exceptions -fno-rtti scripts/native/test_combat_logic.cpp -o .work/native-build/test_combat_logic
timeout 20 .work/native-build/test_combat_logic
g++ -std=c++20 -O1 -Wall -Wextra -Werror -fno-exceptions -fno-rtti scripts/native/test_combat_native.cpp -o .work/native-build/test_combat_native
timeout 20 .work/native-build/test_combat_native
python3 -B scripts/re/scope_probe_analyze.py --selftest
g++ -std=c++20 -O1 -Wall -Wextra -Werror -fno-exceptions -fno-rtti scripts/native/test_graph_probe.cpp -o .work/native-build/test_graph_probe
timeout 20 .work/native-build/test_graph_probe
python3 -B scripts/re/graph_probe_analyze.py --selftest
g++ -std=c++20 -O1 -Wall -Wextra -Werror -fno-exceptions -fno-rtti scripts/native/test_collision_probe.cpp -o .work/native-build/test_collision_probe
timeout 20 .work/native-build/test_collision_probe
python3 -B scripts/re/collision_probe_analyze.py --selftest
g++ -std=c++20 -O1 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -pthread scripts/native/test_collision_observer.cpp -o .work/native-build/test_collision_observer
timeout 30 .work/native-build/test_collision_observer
# the C++ probe log must be readable by the analyzer: handle 0xe1 run of 4 consecutive logged updates, 0xe2 a single appearance
python3 -B scripts/re/collision_probe_analyze.py .work/native-build/collision_probe_sample.log > .work/native-build/collision_probe_report.txt
grep -q "handle 0xe1: 1 run(s): seq 1-7 now 100-148 len 4" .work/native-build/collision_probe_report.txt
grep -q "handle 0xe2: 1 run(s): .* len 1" .work/native-build/collision_probe_report.txt
grep -q "no verdict on current contact" .work/native-build/collision_probe_report.txt
# the C++ graph probe's text format must be readable by the offline analyzer (node 5 id 105, entry lists, def id 4242, choice 6)
python3 -B scripts/re/graph_probe_analyze.py .work/native-build/graph_probe_sample.log > .work/native-build/graph_probe_report.txt
grep -q "node 0x[0-9a-f]*\\[5\\] id=105 type=1 form=4 entry=0" .work/native-build/graph_probe_report.txt
grep -q "req=\\[7, 8\\] post=\\[3\\] hash=\\['deadbeef:\\?', '00001234:\\?'\\] def={'id': 4242, 'category': 6}" .work/native-build/graph_probe_report.txt
grep -q "call seq=1 phase=1 cur=4 K(cur+0x24)=2 cand_list=\\[5, 7, 6, 9\\] ret=6" .work/native-build/graph_probe_report.txt
grep -q "choice seq=1 .* -> 6 phase2=1" .work/native-build/graph_probe_report.txt
# the C++ probe's text format must be readable by the offline analyzer and expose the injected counter
python3 -B scripts/re/scope_probe_analyze.py .work/native-build/scope_probe_sample.log | grep -q "candidate W+0x10 u64 first=5001 last=5008 steps=\[1\]"
# header must also build with the production Windows flags
x86_64-w64-mingw32-clang++ -std=c++20 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -static -c scripts/native/test_combat_logic.cpp -o .work/native-build/test_combat_logic.win.o
x86_64-w64-mingw32-clang++ -std=c++20 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -static -c scripts/native/test_combat_native.cpp -o .work/native-build/test_combat_native.win.o
x86_64-w64-mingw32-clang++ -std=c++20 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -static -c scripts/native/test_graph_probe.cpp -o .work/native-build/test_graph_probe.win.o
x86_64-w64-mingw32-clang++ -std=c++20 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -static -c scripts/native/test_collision_probe.cpp -o .work/native-build/test_collision_probe.win.o
x86_64-w64-mingw32-clang++ -std=c++20 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -static -c scripts/native/test_collision_observer.cpp -o .work/native-build/test_collision_observer.win.o
echo "combat_mod.h + combat_native.h + scope/graph probes: host tests passed; Win64 compile ok"
