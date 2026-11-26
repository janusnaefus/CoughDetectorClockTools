# Rebuilding the Linux converter

The r2 Linux executable statically links GNU C Library (glibc) and GCC runtimes.
The package contains the utility's complete source and a build command under
`source/`, runtime notices under `licenses/`, and the matching Ubuntu glibc
source archive, distribution patches and source description under
`runtime-source/`. Its provenance manifest records the signed Ubuntu snapshot
used for the source download and each file's size/SHA256.

The linked glibc version is2.39-0ubuntu8.9. With `dpkg-source` installed, unpack
the supplied description and tarballs using:

```sh
dpkg-source -x glibc_2.39-0ubuntu8.9.dsc
```

Its `INSTALL`, `README` and `debian/` files contain the upstream and Ubuntu build
instructions. Install a compatible glibc development build in your own toolchain
or sysroot, then rebuild the converter using `source/build-linux.txt`. An ordinary
dynamic rebuild can omit `-static`. The converter also needs a C++17 compiler;
the tested build used Ubuntu GCC13.3.0.

You may modify and relink this utility with a modified compatible LGPL runtime,
and reverse engineer this utility for debugging those modifications. The GNU
runtime licenses, copyright notices and GCC Runtime Library Exception are
included in the package. No proprietary recording-format library is required.

The original runtime sources are also available through the
[Ubuntu source archive](https://packages.ubuntu.com/source/noble-updates/glibc)
and the [Ubuntu snapshot service](https://snapshot.ubuntu.com/). The included
source copy remains tied to the exact runtime version, independent of later
archive updates.
