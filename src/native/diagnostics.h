#pragma once

// Production has no diagnostic producers; explicit debug builds retain them.
#ifndef CRMOD_DIAGNOSTICS
#define CRMOD_DIAGNOSTICS 0
#endif
static_assert(CRMOD_DIAGNOSTICS==0 || CRMOD_DIAGNOSTICS==1);
namespace diagnostics { inline constexpr bool enabled=CRMOD_DIAGNOSTICS!=0; }
#if CRMOD_DIAGNOSTICS
#define CRMOD_DIAGNOSTIC(...) do { __VA_ARGS__; } while(0)
#else
#define CRMOD_DIAGNOSTIC(...) do {} while(0)
#endif
