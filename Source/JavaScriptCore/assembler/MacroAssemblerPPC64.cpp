/*
 * Copyright (C) 2026 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "MacroAssembler.h"

#if ENABLE(ASSEMBLER) && CPU(PPC64LE)

// Phase 1 skeleton. The Probe API trampoline (which saves the CPU state,
// calls a C++ callback, then restores and returns) is the main reason an
// arch MacroAssembler has a .cpp: the trampoline must be handwritten
// assembly that matches the frame layout MacroAssembler::probe() sets up.
// We will implement that trampoline in Phase 3 alongside the real
// MacroAssembler::probe() emission. For now, trap at runtime if anything
// tries to emit a probe — no JIT code reaches this path in a C_LOOP
// build, and Phase 1's JIT-enabled build will fail (loudly) at runtime
// rather than silently emit wrong code.

namespace JSC {

void MacroAssembler::probe(Probe::Function, void*)
{
    UNREACHABLE_FOR_PLATFORM();
}

} // namespace JSC

#endif // ENABLE(ASSEMBLER) && CPU(PPC64LE)
