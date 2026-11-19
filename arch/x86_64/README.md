Not started. Planned architecture directory (see arch/README.md).

## Failure diagnosis
A red square in the top-left corner = the CPU has no 64-bit mode (Limbo: use CPU qemu64 or max, not qemu32). Normal boot progress is shown in the boot log.
magenta = exception handler loaded (each stays visible for a few seconds). A red bar with rows of white/blue squares = a CPU exception happened inside the panic screen itself:
row 1 = RIP (64 bits, white = 1), row 2 = exception number (upper 32 bits) + error code (lower 32 bits), row 3 = CR2.
