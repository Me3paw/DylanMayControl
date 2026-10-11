#include "../../src/native/loot_policy.h"
#include <cassert>
#include <cstdio>
int main(){using namespace loot_policy;assert(eligible_name("CURRENCY_CASH"));assert(eligible_name("Living Shard"));assert(eligible_name("Currency_Warped_Resonance"));assert(!eligible_name("Talent Points"));assert(!eligible_name("Currency_Reset_Item"));assert(!eligible_name("QuestSource"));assert(scaled_amount(7,3,10,100)==21);assert(scaled_amount(7,1,10,100)==7);assert(scaled_amount(7,10,90,100)==10);assert(scaled_amount(-2,3,10,100)==-2);assert(scaled_amount(2147483647,10,1,2147483647)==2147483646);puts("loot eligibility and bounded scaling PASS");}
