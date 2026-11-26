# Magnetic CSV export

[Download the Windows/Linux CSV converter](https://github.com/janusnaefus/CoughDetectorClockTools/releases/tag/magnetic-csv-v0.1.0)

This small computer tool converts the recorder's magnetic BIN files into CSV
for a spreadsheet. It runs on Windows x64 and Linux x86-64 without Python or
administrator access. It uses copied files; it does not connect to the detector
or change recording power consumption.

Copy the recording folder from the detector first, including `gmeta00.jsn` and
every `g0000000.bin`, `g0000001.bin`, and subsequent magnetic chunk you want.
Older uppercase names work too. Keep the original files.

On Windows, extract the ZIP and double-click `detector-magnetic-csv.exe`, then
select `gmeta00.jsn` in the copied recording folder. Alternatively, drop that
folder onto the EXE. A message shows where the export was saved. The executable
is currently unsigned; its published SHA256 identifies the supplied download.

On Linux, extract the ZIP, open a terminal in that directory, and run:

```sh
chmod +x detector-magnetic-csv-linux-x86_64
./detector-magnetic-csv-linux-x86_64 '/path/to/copied recording'
```

The r2 Linux x86-64 binary includes its runtime and does not require an installed
glibc/libstdc++ version. Ubuntu24.04 is tested; other distributions/kernels have
not been tested. Utility source, a build command and the corresponding runtime
source/notices are included. The original r1 binary requires glibc2.38 or newer.
For scripted Windows use, run `detector-magnetic-csv.exe --cli "C:\path\to\recording"`.

The new `magnetic_csv` subfolder contains one CSV per magnetic chunk, the exact
metadata bytes as `gmeta00.json`, a manifest and a short field guide. An existing
`magnetic_csv` is never overwritten; move or rename it before exporting again.
The original BIN/JSN/WAV/log files are not changed.

Import each CSV into Excel or LibreOffice Calc with **Comma** as delimiter and
all columns initially set to **Text**. This preserves64-bit counters and raw hex
exactly. Convert selected raw magnetic columns to numbers in your analysis copy.
An empty field means no observation was present; zero is an observed zero.
`x_raw`, `y_raw` and `z_raw` are signed sensor-native counts. The current LIS2MDL
configuration uses nominal0.15µT/count; configuration remains in the JSON. The
converter applies no calibration, axis transform, interpolation or smoothing.

Timing, epoch, sequence, status and quality fields retain the recorder's values.
TIM5 ticks are not UTC and do not establish simultaneous magnetic/IMU/microphone
conversion. Sequence gaps and coherence claims remain visible; conversion does
not qualify acquisition completeness or sensor alignment.

The converter checks header/version/length, record CRC, known flags, canonical
absence encoding, same-session chunk headers and the exact metadata hash. It
copies JSON without interpreting or validating its configuration semantics.
`manifest.csv` preserves every64-byte header; each row's `raw_record_hex`
preserves every64-byte record. Together with the unchanged JSON, these permit
byte-for-byte reconstruction of every input BIN, including empty chunks.

At most4096chunks of16384records each are accepted. Memory is bounded to one
chunk plus the small metadata file. CSV is larger than BIN because it includes
both readable fields and the exact record encoding; this affects computer disk
space only. A failed conversion removes its newly created temporary output and
leaves source files and existing exports intact.
