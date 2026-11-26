# Converter verification

Public release magnetic-csv-v0.1.0 uses the previously tested r2 ZIP unchanged:
20,191,471 bytes, SHA256
`d34947c4abc55e49a79d21197034ee8995a84fa41379a14841b5d8df24f9fd0f`.

Windows CLI/file-picker use passed; the binary is unsigned. Linux x86-64 is
statically linked, with no ELF interpreter/dynamic dependencies, and passed
nine tests on Ubuntu24.04. Other distributions/kernels are untested. Real407-
and13411-record/two-chunk examples produced identical platform outputs and
reconstructed every input BIN byte; the exact metadata bytes were preserved.
Tests cover signed/64-bit extremes, absence versus zero, corruption/truncation,
flags, mixed sessions, Unicode/case handling and existing-output protection.
The tool does not qualify sensor acquisition, alignment or metadata semantics.

The release includes complete utility source, rebuilding/relinking instructions,
runtime notices and the matching glibc2.39-0ubuntu8.9 source/Ubuntu patches.
Only converter files are published here; test recordings are not distributed.
