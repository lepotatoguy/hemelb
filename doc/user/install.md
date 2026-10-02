# Installing HemeLB on macOS or Linux

`Scripts/install_hemelb.sh` builds HemeLB and installs the geometry
tool and Python tools. It supports macOS (Apple Silicon and Intel) and
Debian/Ubuntu Linux.

```sh
git clone -b fix/hemelb-improvements https://github.com/lepotatoguy/hemelb.git
cd hemelb
Scripts/install_hemelb.sh
```

The script is on the `fix/hemelb-improvements` branch; the `-b` option
selects it. You need `git` and an internet connection. On macOS, running
`git` for the first time offers to install the Xcode Command Line Tools,
which the build also needs. The script builds from the checkout you run it in.
Confirm the branch with `git branch --show-current` before installing.

The script does not ask questions. By default it:

1. Installs system packages: Homebrew packages on macOS (and Homebrew
   itself if it is missing), or apt packages on Linux (using `sudo`).
2. Builds HemeLB and the dependencies it cannot find (for example ParMETIS
   and TinyXML) into `~/.local/hemelb`, then runs `hemelb-tests`.
3. Creates a conda environment called `gmy-tool` with exactly the tested
   package versions (from `geometry-tool/conda-lock/`) and installs the
   geometry tool and the Python tools into it. If conda is not installed, Miniforge is
   installed into `~/miniforge3`.

The solver and geometry tool use separate environments. On Apple Silicon the
solver is built natively, while the geometry environment uses the repository's
`osx-64` lock under Rosetta. Do not use that conda environment to configure a
native solver build.

## Installation options

Run `Scripts/install_hemelb.sh --help` for the complete option list.

| Option | Effect |
| --- | --- |
| `--prefix DIR` | Install HemeLB into `DIR` instead of `~/.local/hemelb` |
| `--jobs N` | Number of parallel build jobs |
| `--env-name NAME` | Name of the conda environment |
| `--env-prefix DIR` | Create the conda environment at a chosen path instead of by name |
| `--no-gui` | Skip macOS GUI launcher setup; it does not remove GUI dependencies |
| `--no-gmy-tool` | Build HemeLB only |
| `--no-system-deps` | Skip Homebrew or apt, if you installed the packages yourself |
| `--no-tests` | Skip running `hemelb-tests`; tests are still built |

Choose a new environment name if `gmy-tool` already exists:

```sh
Scripts/install_hemelb.sh --env-name hemelb-tools --jobs 4
conda activate hemelb-tools
```

For an environment created with `--env-prefix`, activate its path instead of a
name. `--no-gmy-tool` skips both Python packages and the conda setup, so it is
suitable when you already have prepared `.gmy` and `.xml` inputs.

After installing:

```sh
export PATH="$HOME/.local/hemelb/bin:$PATH"   # add to your shell profile
conda activate gmy-tool                        # for hlb-gmy-gui, hlb-gmy-cli, ...
mpirun -n 4 hemelb -in input.xml -out results
```

Next, follow [Quick start](getting-started.md) for a quick start from a
surface to results.

## Platform notes

These are handled by the script; they are listed so that a manual install
can repeat them.

- **CMake 4.** The bundled ParMETIS 4.0.2 build is configured with
  `CMAKE_POLICY_VERSION_MINIMUM=3.5`, because its own CMake files declare a
  minimum version that CMake 4 no longer accepts.
- **One CMake pass.** Dependencies that the super build compiles itself
  (such as TinyXML) are found by the main build in the same run, so a
  second `cmake` pass is not needed.
- **VMTK on Apple Silicon.** VMTK 1.5 is only published for `osx-64` and
  `linux-64`. On Apple Silicon the conda environment is created with
  `CONDA_SUBDIR=osx-64` and runs under Rosetta 2, which the script installs
  if needed (`softwareupdate --install-rosetta --agree-to-license`).
- **CGAL with Clang.** The CGAL 5.6 headers contain `this->base() ==
  nullptr` comparisons in `CGAL/boost/graph/iterator.h` that current Clang
  rejects. The geometry tool's CMake build compiles against a corrected copy
  of that one header in its build folder; the installed CGAL is not
  changed.
- **VMTK and pip.** VMTK is only published on conda-forge, not PyPI.
  `geometry-tool/setup.py` checks whether VMTK is already installed
  (importable, or listed in the active conda environment) and only then
  leaves it out of the requirements, so a plain `pip install
  './geometry-tool[gui]'` works in the conda environment and still refuses
  to install without VMTK. The script uses `pip install --no-deps
  --no-build-isolation` anyway, so every dependency stays at its locked
  conda version.
  Older guides said to comment out the VMTK line in `setup.py` or run
  `bodge-packages-for-setuptools.sh`; neither is needed any more.
- **CGAL version.** The geometry tool needs CGAL 5 (CGAL 6 removed a header
  it uses). The environment pins CGAL 5.6.1, and in a conda environment the
  tool's CMake searches `$CONDA_PREFIX` first, so a Homebrew CGAL 6 is not
  picked up by mistake.
- **Exact versions.** The conda environment is created from
  `geometry-tool/conda-lock/<platform>.txt`, which fixes every package to an
  exact build, so another installation reuses those locked packages.
  The pin sources and fallback library versions are listed in
  [INSTALL](../../INSTALL). Compiler, CMake, MPI, and other system packages
  are selected by Homebrew or apt at installation time and are not locked by
  this repository.
- **macOS GUI.** `hlb-gmy-gui` needs a framework build of Python to open
  windows. The script installs `python.app` and runs
  `geometry-tool/macos-fix-gui-launcher.py` so `hlb-gmy-gui` can be run
  directly.

## Verify the installation

```sh
command -v hemelb
hemelb-tests
conda activate gmy-tool
hlb-gmy-cli --help
hlb-dump-extracted-properties --help
```

Optional: `command -v hemelb-confcheck` confirms that the XML-checking tool is available.

Substitute your environment name if customized. The installer runs the C++ unit
executable; to run the MPI restart test too, use the CTest command in
[build and run](main-application.md#test). Continue with
[quick start](getting-started.md), or consult
[troubleshooting](troubleshooting.md) if installation fails.

## Uninstalling

The commands below remove the default solver prefix and tool environment.
Replace them with your chosen paths and names; saved runs outside those
locations are separate.

```sh
rm -rf ~/.local/hemelb
conda env remove -n gmy-tool
```
