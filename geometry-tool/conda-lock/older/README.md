# Older Linux environments

Two exact package lists from earlier working setups, kept for reference.
The install script does not use them; it uses `../linux-64.txt` and
`../osx-64.txt`, which are tested in CI.

| File | Python | VTK | VMTK | wxPython | Packages |
| --- | --- | --- | --- | --- | --- |
| `linux-64-py3.8-headless-2024-12-06.txt` | 3.8.20 | 9.1.0 | 1.5.0 | 4.1.1 | 255 |
| `linux-64-py3.8-gui-2025-04.txt` | 3.8.20 | 9.1.0 | 1.5.0 | 4.2.0 | 247 |

Both are Linux x86_64 only. They mix conda-forge with Anaconda's `defaults`
channel (`repo.anaconda.com`), whose terms of service may require a licence
for some organisations. On 2026-09-30 every package URL in both files could
still be downloaded; building the geometry tool in them has not been
re-tested since they were written.

Use one with:

```sh
conda create -n gmy-tool-old --file linux-64-py3.8-gui-2025-04.txt
```
