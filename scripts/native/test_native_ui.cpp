#include "../../src/native/native_ui.h"
#include <cassert>
#include <cstdio>
#include <string>
#ifdef NDEBUG
#error "Native UI checks require assertions enabled"
#endif
using namespace loadout_control;
using namespace native_ui;
int main() {
    assert(make_owned_bootstrap("", "body{}").empty());
    assert(make_owned_bootstrap("void 0;", "").empty());
    assert(make_owned_bootstrap(std::string("x\0y", 3), "body{}").empty());
    assert(make_owned_bootstrap("void 0;", std::string("x\0y", 3)).empty());
    assert(make_owned_bootstrap(std::string(128 * 1024, 'x'), "x").empty());
    auto bootstrap = make_owned_bootstrap("window.CRLoadoutPanel={};",
        "a{content:\"\\\"}\n\xe2\x80\xa8\xe2\x80\xa9");
    assert(!bootstrap.empty() && bootstrap.size() <= 256 * 1024);
    assert(bootstrap.find("\\u000a") != std::string::npos);
    assert(bootstrap.find("\\u2028\\u2029") != std::string::npos);
    assert(bootstrap.find("function findAnchor") != std::string::npos);
    assert(bootstrap.find("cr-loadout-open") == std::string::npos);
    assert(bootstrap.find("root.setTimeout") != std::string::npos);
    assert(make_owned_bootstrap("void 0;", "body{}",std::string(384*1024+1,'x')).empty());
    assert(make_owned_bootstrap("void 0;", "body{}",std::string("x\0y",3)).empty());
    MenuEvent e{};
    const std::string maximum =
        "{\"world_epoch\":\"18446744073709551615\",\"id\":4294967295,"
        "\"slot\":2,\"channel\":1,\"action\":0}";
    assert(parse_command(maximum, e));
    assert(e.world_epoch == UINT64_MAX && e.id == UINT32_MAX && e.slot == 2 &&
           e.channel == Channel::Secondary && e.action == Action::Configure);
    const std::string zero_id =
        "{\"action\":0,\"channel\":0,\"slot\":0,\"id\":0,\"world_epoch\":\"1\"}";
    assert(parse_command(zero_id, e) && e.id == 0);
    const char* bad[] = {
        "{\"action\":0,\"channel\":0,\"slot\":0,\"id\":0,\"world_epoch\":1}",
        "{\"action\":0,\"channel\":0,\"slot\":0,\"id\":4294967296,\"world_epoch\":\"1\"}",
        "{\"action\":0,\"channel\":0,\"slot\":0,\"id\":0,\"world_epoch\":\"18446744073709551616\"}",
        "{\"action\":0,\"channel\":0,\"slot\":0,\"id\":0,\"world_epoch\":\"01\"}",
        "{\"action\":0,\"channel\":0,\"slot\":0,\"id\":0,\"world_epoch\":\"0\"}",
        "{\"action\":0,\"channel\":0,\"slot\":0,\"id\":0,\"id\":1}",
        "{\"action\":0,\"channel\":0,\"slot\":0,\"id\":0,\"extra\":1}",
        "{\"action\":3,\"channel\":0,\"slot\":0,\"id\":0,\"world_epoch\":\"1\"}",
        "{\"action\":0,\"channel\":2,\"slot\":0,\"id\":0,\"world_epoch\":\"1\"}",
        "{\"action\":0,\"channel\":0,\"slot\":4,\"id\":0,\"world_epoch\":\"1\"}",
        "{\"action\":0,\"channel\":0,\"slot\":3,\"id\":0,\"world_epoch\":\"1\"}",
        "{\"action\":0,\"channel\":0,\"slot\":0,\"id\":1e0,\"world_epoch\":\"1\"}"
    };
    for (auto input : bad) assert(!parse_command(input, e));
    assert(!parse_command(zero_id + "false", e));
    std::string embedded = zero_id;
    embedded.push_back('\0'); embedded += zero_id;
    assert(!parse_command(embedded, e));
    assert(!parse_command(std::string(256, ' '), e));
    assert(parse_command(zero_id, e));
    CommandMailbox box;
    assert(!box.enqueue(1, e).ticket);
    assert(box.bind_generation(1));
    assert(!box.bind_generation(2));
    std::array<Receipt, 32> receipts{};
    for (auto& receipt : receipts) {
        receipt = box.enqueue(1, e);
        assert(receipt.ticket && receipt.view_generation == 1);
    }
    assert(!box.enqueue(1, e).ticket);
    ClaimedCommand running{};
    for (const auto receipt : receipts) {
        assert(box.claim(running));
        assert(running.receipt.ticket == receipt.ticket);
        assert(box.complete(receipt, Result::Rejected));
    }
    assert(!box.enqueue(1, e).ticket);
    Result result = Result::Configured;
    assert(!box.take_reply({receipts[0].ticket, 2}, result));
    assert(box.take_reply(receipts[0], result) && result == Result::Rejected);
    auto newer = box.enqueue(1, e);
    assert(newer.ticket > receipts.back().ticket);
    assert(box.claim(running) && running.receipt.ticket == newer.ticket);
    box.retire(999);
    assert(box.complete(newer, Result::Queued));
    assert(box.take_reply(newer, result) && result == Result::Queued);
    auto before_release = box.enqueue(1, e);
    assert(box.claim(running) && running.receipt.ticket == before_release.ticket);
    box.retire(1);
    assert(!box.enqueue(1, e).ticket && !box.claim(running));
    assert(!box.bind_generation(1));
    assert(box.bind_generation(2));
    auto current = box.enqueue(2, e);
    assert(box.claim(running) && running.receipt.ticket == current.ticket);
    assert(box.complete(before_release, Result::Configured));
    assert(!box.take_reply(before_release, result));
    assert(!box.take_reply(current, result));
    assert(box.complete(current, Result::Cleared));
    assert(box.take_reply(current, result) && result == Result::Cleared);
    assert(!box.take_reply(current, result));
    puts("PASS: strict native UI command parsing and bounded lifecycle/reply mailbox; native binding untested");
}
