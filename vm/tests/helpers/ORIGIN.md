# Deterministic CPU test helpers

`atari-6502-cpu.mjs` and `c64-terminal-cpu.mjs` were copied from the maintained
AGI-64 checkout's `test/helpers/` on September 4, 2026, with only line-ending
normalization. They run software verification; no helper is shipped in a VM
or firmware image. The AGI keyboard test and reset/wire replay tests reuse the
existing tests with local source paths and the exact AGIVM terminal options.
There is no AGI-64 checkout dependency for building the runtime/client or for
the deterministic input and wire tests. VICE remains an explicit local tool.
