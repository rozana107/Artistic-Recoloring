# Artistic Recoloring of Image Oversegmentations

A straightforward **C++14 and OpenCV** implementation of the main recoloring algorithm described by **Rosa Azami and David Mould** in *Artistic Recoloring of Image Oversegmentations*.

The program turns an image abstraction into a region graph and assigns colors from a small palette while preserving local color differences and relative lightness. The code uses ordinary functions, loops, vectors, and a priority queue. It runs on the CPU and requires no trained models.

![Input photographs and recolorings with the sunset and ocean palettes](results/preview.png)

From left to right: input photograph, sunset palette, and ocean palette. See [all results and measurements](RESULTS.md) or the [full comparison image](results/comparison.jpg).

## Features

- Direct integration with the supplied `ImageAbstraction` implementation, preserving its final region labels and representative colors.
- Palette difference histogram matching, a widest-path tree, and greedy color assignment with relative lightness constraints.
- RGB Euclidean and CIE76 Lab color distances.
- Reusable label maps and abstraction colors for fast palette changes.
- Optional OpenCV graph segmentation and SLICO comparisons.
- Separate preprocessing and recoloring timings, batch processing, and correctness tests.

This is an independent reimplementation. Exact agreement with the supplied abstraction code has been checked; exact reproduction of the paper's published figures is not established. See [reproducibility](#reproducibility-and-limitations).

## Requirements

| Dependency | Requirement |
|---|---|
| C++ compiler | C++14 support |
| CMake | 3.16 or newer; `--fresh` requires 3.24 or newer |
| OpenCV | Version 4, with `core`, `imgproc`, `imgcodecs`, and contrib `ximgproc` |
| ImageAbstraction | The separately supplied source folder described below |
| PowerShell | For the Windows build and batch scripts |

Windows x64 with GCC/MinGW and OpenCV 4.12.0 is the validated configuration. OpenCV libraries must be compatible with the selected compiler; MSVC libraries cannot be linked into a GCC/MinGW build.

**The abstraction source is a required external build dependency and is not bundled in this repository.** Place the supplied folder beside this project, or set `IMAGE_ABSTRACTION_DIR` when configuring CMake:

```text
parent/
├── ArtisticRecoloring/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   └── ...
└── ImageAbstraction/
    ├── include/
    │   └── image_abstraction.h
    └── src/
        ├── image_abstraction.cpp
        ├── crgf.cpp
        └── slic_background.cpp
```

CMake compiles these three source files unchanged. The dependency is required even when choosing an alternative segmentation method at runtime.

## Build

Run these commands from the repository root using a compiler environment with CMake and a compatible OpenCV installation:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOpenCV_DIR="C:/path/to/opencv/package" -DIMAGE_ABSTRACTION_DIR="C:/path/to/ImageAbstraction"
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

Replace the example paths with your installations. `OpenCV_DIR` must point to the directory containing `OpenCVConfig.cmake`. `IMAGE_ABSTRACTION_DIR` defaults to `../ImageAbstraction` and can be omitted for the sibling layout above. Select a CMake generator if your compiler requires one.

The Windows Release executable is written to `bin/recolor.exe`. MinGW builds use static linking and require compatible static OpenCV libraries. Other builds may require OpenCV DLLs on `PATH`. Generated binaries and build directories are excluded by `.gitignore`; build the executable after cloning.

### Shared Windows tools

On the original workstation, GCC, CMake, and static OpenCV are installed together under `D:/WorkStation/tools`. The helper script uses this location automatically, builds Release, and runs the tests:

```powershell
.\build.ps1
```

To use the same portable tools layout elsewhere:

```powershell
.\build.ps1 -ToolsRoot "C:/tools" -ImageAbstractionDir "C:/projects/ImageAbstraction"
```

The expected tools layout contains `w64devkit/bin`, `cmake-3.31.8-windows-x86_64/bin`, and `opencv-install/x64/mingw/staticlib`. The script also accepts `-OpenCVDir`. Use `-Fresh` after changing compilers or dependency locations. If PowerShell blocks script execution, invoke the script for that process with:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

## Quick start

From the **repository root** in PowerShell:

```powershell
.\bin\recolor.exe images\124084.jpg recolored.png
```

This runs the supplied abstraction method and applies the built-in sunset palette. Input dimensions are preserved by default. The result is saved as `recolored.png` in the current directory. Use PNG output to preserve exact palette colors.

If your terminal is already inside **`bin`**, use:

```powershell
.\recolor.exe ..\images\124084.jpg recolored.png
```

PowerShell requires the `.\` prefix to run an executable from the current directory. In this example the output is saved inside `bin`.

### Change the palette

```powershell
.\bin\recolor.exe images\124084.jpg ocean.png --palette palettes\ocean.txt
```

Palette files contain one RGB hexadecimal color per line, with an optional `#` prefix:

```text
241332
563474
9B63C4
E83F6F
FF795C
FFB47D
FFE1A8
```

Blank lines and lines beginning with `//` are ignored. Up to 256 unique colors are supported. [Sunset](palettes/sunset.txt), [ocean](palettes/ocean.txt), and [grayscale](palettes/grayscale.txt) palettes are included.

### Reuse the abstraction for faster palette changes

Save both the final labels and the abstraction image during the first run:

```powershell
.\bin\recolor.exe images\124084.jpg sunset.png --save-labels labels.yml --abstraction abstraction.png
.\bin\recolor.exe images\124084.jpg ocean.png --labels labels.yml --region-image abstraction.png --palette palettes\ocean.txt
```

The second command skips abstraction. **Both cached files are needed to preserve the same region membership and colors.** Supplying `--labels` alone uses region means from the original photograph, which changes the recoloring input.

Final label files from the standalone abstraction program can also be used. Read its `labels` matrix, rather than `owners`: `owners` describes overlapping regions and includes background values.

## Main options

```powershell
.\bin\recolor.exe --help
```

| Option | Default | Purpose |
|---|---|---|
| `--method abstraction\|graph\|slico` | `abstraction` | Segmentation method |
| `--palette FILE` | Built-in sunset | Palette file |
| `--metric rgb\|lab` | `rgb` | RGB Euclidean or CIE76 Lab distance |
| `--labels FILE` | None | Reuse an existing label map |
| `--region-image FILE` | None | Region colors to accompany `--labels` |
| `--save-labels FILE` | None | Save final integer labels |
| `--abstraction FILE` | None | Save the fresh abstraction rendering |
| `--original-means` | Disabled | Use original-photo region means |
| `--max-side N` | `0` | Resize before processing; zero retains original size |
| `--root X Y` | Image center | Starting pixel for the tree |
| `--repeat N` | `1` | Recoloring repetitions for median timing |
| `--threads N` | `1` | OpenCV thread count |
| `--flat FILE` | None | Save the region colors used for recoloring |
| `--boundaries FILE` | None | Save boundaries over the input image |

The supplied abstraction defaults are mask `160`, gamma `1`, detail threshold `3`, minimum seed size `11`, hops `10`, growth multiplier `100`, histogram bins `64`, and requested background regions `580`. Corresponding flags are `--mask`, `--gamma`, `--detail-threshold`, `--seed-min-size`, `--hops`, `--growth`, `--bins`, and `--background`.

`--save-filtered FILE` saves the CRGF result; `--filtered FILE` reuses it during fresh abstraction. The cache must come from the same input and intended settings, with matching processed dimensions and type. Cache provenance is not checked automatically.

`--no-histogram`, `--no-lightness`, and `--lightness-tolerance N` support controlled comparisons. Lightness tolerance defaults to zero and is measured in CIE L* units.

Alternative segmentation examples:

```powershell
.\bin\recolor.exe examples\starfish.jpg graph.png --method graph --k 150 --min-size 20
.\bin\recolor.exe examples\starfish.jpg slico.png --method slico --regions 2500 --iterations 10
```

## Batch processing

Process the six supplied photographs, or the six photographs extracted from the paper:

```powershell
.\run-examples.ps1 -InputDirectory images -OutputDirectory results\images
.\run-examples.ps1
```

The second command writes to `results/examples/`. Existing outputs at the selected destination are overwritten; choose another `-OutputDirectory` to keep a separate experiment.

For each image, the default abstraction run saves:

```text
abstraction.png    Rendered abstraction and reusable region colors
labels.yml         Exact final integer labels
sunset.png         Sunset recoloring
ocean.png          Ocean recoloring using cached labels and colors
timing.txt         Combined timing log
```

The script also writes `timings.csv` and checks that input file hashes remain unchanged. Abstraction runs once per image; sunset recoloring uses seven repetitions by default. Set `-Repeat N` to change that count. Add `-Diagnostics` to save CRGF and flat-color images. The retained [HTML gallery](results/index.html) can be opened locally in a browser; GitHub displays its source.

## Method

1. Run the supplied CRGF, seed selection, overlapping region growth, SLIC background, and final cleanup pipeline.
2. Use its final `Result.labels` and modal region colors from `Result.image`.
3. Build a region adjacency graph using four-neighbor pixel boundaries.
4. Match the distribution of neighboring region differences to palette differences.
5. Construct a widest-path tree, including the paper's region-area weighting.
6. Assign palette colors greedily, enforcing relative lightness where possible, and render the regions.

The root is the region containing the center pixel unless `--root` is supplied. It receives the nearest palette color. When no palette color satisfies a child's lightness constraint, assignment considers the full palette.

Supplied region membership is preserved, including diagonal fragment attachments and disconnected pieces with the same ID. IDs are only compacted in row-major order. Different labels remain separate even when their representative colors match. The standalone core API also exposes `Settings::splitDisconnectedLabels`.

## Results and performance

The following are retained measurements on Windows x64 with an Intel Core i7-7800X at 3.50 GHz, GCC 16.2.0, OpenCV 4.12.0, a Release build, and one OpenCV thread. Images were processed at their original dimensions using default abstraction settings and the seven-color sunset palette.

| Image | Dimensions | Regions | Fresh abstraction | Recoloring median |
|---|---:|---:|---:|---:|
| `124084.jpg` | 481 × 321 | 1,451 | 8.56 s | 5.74 ms |
| `landscape.jpg` | 640 × 426 | 5,175 | 15.26 s | 14.91 ms |
| `parrots.jpg` | 1024 × 640 | 5,902 | 37.40 s | 24.88 ms |
| `cabbage1024.jpg` | 1024 × 768 | 11,548 | 43.04 s | 56.58 ms |
| `bicycle.jpg` | 1024 × 682 | 27,795 | 38.46 s | 67.06 ms |

**Fresh abstraction dominates processing time.** Recoloring medians cover seven runs of the recoloring pipeline and exclude image loading, label parsing, and output encoding. Cached labels and colors avoid the abstraction cost. A separate fresh run on `124084.jpg` took 9.284 seconds including loading and requested outputs.

These are measurements of this implementation, not a direct speed comparison with the paper. See [RESULTS.md](RESULTS.md) for all twelve images, timing methodology, and verification records.

## Tests and verification

```powershell
ctest --test-dir build -C Release --output-on-failure
```

The four CTest entries cover recoloring correctness, abstraction/label integration, CLI help, and rejection of an invalid parameter. Correctness checks include an independent widest-path oracle, histogram matching, lightness behavior, region consistency, and deterministic output.

Retained validation records in [results/verification](results/verification/) show:

- Pixel-for-pixel agreement with standalone abstraction labels and images on all six supplied photographs.
- Exact palette membership and one output color per region for both palettes on all twelve inputs.
- Unchanged input images and abstraction source hashes.
- Equality between fresh processing and cached-label/color reuse on the checked sample.

The six paper-extracted inputs have no standalone reference label maps; the exact reference comparison applies only to the six supplied photographs.

## Reproducibility and limitations

The supplied abstraction source is compiled unchanged, but that source itself documents adaptations, cleanup fixes, deterministic ties, and an assumed CRGF gamma. Matching this supplied code does not establish pixel equality with the authors' historical executable. Exact published figures also depend on original palettes and settings that are not fully specified.

The recoloring implementation makes these explicit choices where executable detail is missing:

- Empirical CDF matching with midpoint ranks for ties and all ordered palette pairs, including self-pairs. Zero input differences remain zero.
- CIE L* ordering from linearized sRGB using OpenCV's D65 coefficients.
- Destination-region area weighting in each traversal direction.
- Deterministic priority-queue and palette-order ties.

CMC, CIE94, CIEDE2000, and optional cross-filtered blending are not implemented. Outputs stay within the palette; faces and separate sky regions can exhibit the artifacts discussed in the paper.

EXIF orientation is ignored. With cached labels, the label map and optional region image must initially match the original input dimensions. `--max-side` resizes cached labels and colors by nearest neighbor; this does not reproduce segmentation computed afresh at the new resolution. Root coordinates refer to processed dimensions.

### Label formats

OpenCV YAML/XML/JSON files use a nonnegative `CV_32SC1` matrix named `labels`. YAML supports IDs above 65,535. The program also accepts 8/16-bit grayscale label PNGs and 8-bit color-coded label PNGs; alpha is ignored. PNG label export requires IDs no larger than 65,535. Use lossless label maps rather than photographs or antialiased previews.

## Repository layout

| Path | Contents |
|---|---|
| [main.cpp](main.cpp) | CLI, abstraction integration, and timing |
| [recolor.cpp](recolor.cpp), [recolor.h](recolor.h) | Graph, histogram matching, tree, and palette assignment |
| [label_io.cpp](label_io.cpp), [label_io.h](label_io.h) | Integer label map input/output |
| [tests.cpp](tests.cpp) | Recoloring correctness checks |
| [abstraction_integration_tests.cpp](abstraction_integration_tests.cpp) | Label/color preservation and integration checks |
| [CMakeLists.txt](CMakeLists.txt), [build.ps1](build.ps1) | Build configuration and Windows helper |
| [run-examples.ps1](run-examples.ps1) | Batch processing and timing export |
| [palettes/](palettes/) | Example palettes |
| [images/](images/), [examples/](examples/) | Supplied and paper-extracted input photographs |
| [results/](results/) | Final comparisons, labels, abstractions, and verification |
| [RESULTS.md](RESULTS.md) | Detailed measurements and validation |

`bin/` and `build/` are generated locally. The shared tools installation and external abstraction source are outside this repository.

## Paper and attribution

Rosa Azami and David Mould. **Artistic Recoloring of Image Oversegmentations.** See the [included paper](ArtisticRecoloring.pdf) for the original method and image credits.

The paper, example photographs, OpenCV, and the supplied abstraction implementation retain their respective authorship and licensing terms. No project code license has been specified in this repository.
