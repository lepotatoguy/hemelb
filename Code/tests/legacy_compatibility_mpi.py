#!/usr/bin/env python3
# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

"""Check direct legacy XML loading and rank-portable extraction v4/v5 restarts."""
import argparse
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import xml.etree.ElementTree as ET

from checkpoint_restart_mpi import RESOURCE_DIR, MPIRUN_FLAGS, make_config, run, checkpoint_at, saved_sites, checkpoint_data_start

MMHG_TO_PA = 133.3223874


def legacy_xml(tree, version):
    root = tree.getroot()
    root.set('version', str(version))
    ET.SubElement(ET.SubElement(root, 'visualisation'), 'display', zoom='1.0')
    sim = root.find('simulation')
    ET.SubElement(sim, 'stresstype', value='1')
    for el in root.iter():
        if el.get('units') == 'Pa':
            el.set('units', 'mmHg')
            el.set('value', format(float(el.get('value')) / MMHG_TO_PA, '.17g'))
    if version == 3:
        dx = float(sim.find('voxel_size').get('value'))
        origin = [float(v) for v in sim.find('origin').get('value').strip('()').split(',')]
        for position in root.findall('./inlets/inlet/position') + root.findall('./outlets/outlet/position'):
            coords = [float(v) for v in position.get('value').strip('()').split(',')]
            position.set('value', '(' + ','.join(format((v - o) / dx, '.17g') for v, o in zip(coords, origin)) + ')')
            position.set('units', 'lattice')
    return tree


def make_legacy_checkpoint(source, offsets, destination, version, float_values):
    data = source.read_bytes()
    count = struct.unpack_from('>Q', data, 68)[0]
    vectors = struct.unpack_from('>I', data, 104)[0]
    name = b'distributions'
    field = struct.pack('>I', len(name)) + name + bytes(3)
    value_offset = 0.125 if version in (4, 7) else 0.0
    if version == 4:
        field += struct.pack('>Id', vectors, value_offset)
    else:
        field += struct.pack('>III', vectors, 0 if float_values else 1, 1 if version == 7 else 0)
        if version == 6: field += struct.pack('>f' if float_values else '>d', 0.0)
        if version == 7:
            field += struct.pack('>ddI', value_offset, 0.0, 7) + b'lattice' + bytes(1)
    if version >= 6:
        header = bytearray(data[:84])
        struct.pack_into('>I', header, 8, version)
        struct.pack_into('>I', header, 80, len(field))
    else:
        dx = struct.unpack_from('>d', data, 12)[0]
        origin = struct.unpack_from('>3d', data, 36)
        header = struct.pack('>III4dQII', 0x686c6221, 0x78747204, version, dx, *origin, count, 1, len(field))
    data_start = checkpoint_data_start(data)
    timestep = struct.unpack_from('>Q', data, data_start)[0]
    body = bytearray(struct.pack('>Q', timestep))
    expected = {}
    old_size = 12 + vectors * 8
    new_size = 12 + vectors * (4 if float_values else 8)
    fmt = f'>{vectors}' + ('f' if float_values else 'd')
    for position in range(data_start + 8, len(data), old_size):
        coord = struct.unpack_from('>III', data, position)
        values = struct.unpack_from(f'>{vectors}d', data, position + 12)
        encoded = struct.pack(fmt, *(v - value_offset for v in values))
        expected[coord] = tuple(v + value_offset for v in struct.unpack(fmt, encoded))
        body.extend(data[position:position + 12]); body.extend(encoded)
    destination.write_bytes(header + field + body)
    off = offsets.read_bytes()
    nranks = struct.unpack_from('>I', off, 12)[0]
    old_offsets = struct.unpack_from(f'>{nranks + 1}Q', off, 16)
    start = len(header) + len(field)
    positions = [start] + [start + 8 + (v - old_offsets[0] - 8) // old_size * new_size for v in old_offsets[1:]]
    destination.with_suffix('.off').write_bytes(off[:16] + struct.pack(f'>{nranks + 1}Q', *positions))
    return expected


def expect_rejected(args, config, output, message):
    result = subprocess.run([args.mpirun, *MPIRUN_FLAGS, '-np', '2', args.hemelb,
                             '-in', str(config), '-out', str(output)], capture_output=True, text=True, timeout=30)
    assert result.returncode != 0
    assert message in result.stdout + result.stderr



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hemelb', required=True)
    parser.add_argument('--mpirun', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='legacy-compatibility-') as directory:
        work = Path(directory)
        shutil.copyfile(RESOURCE_DIR / 'large_cylinder.gmy', work / 'large_cylinder.gmy')
        baseline_config = work / 'baseline.xml'
        make_config(baseline_config)
        base_tree = ET.parse(baseline_config)
        root = base_tree.getroot()
        ET.SubElement(root.find('simulation'), 'reference_pressure', units='Pa', value='2')
        root.find('initialconditions/pressure/uniform').set('value', '3')
        for mean in root.findall('.//condition/mean'): mean.set('value', '3')
        base_tree.write(baseline_config)
        baseline = work / 'baseline'
        run(args.mpirun, args.hemelb, 2, baseline_config, baseline)
        expected = saved_sites(checkpoint_at(baseline, 4))
        for version in (3, 5):
            for file_pressure in (False, True):
                config = work / f'v{version}-{file_pressure}.xml'
                tree = legacy_xml(ET.parse(baseline_config), version)
                profile = work / f'pressure-{version}.txt'
                profile.write_text(''.join(f'{t} {3 / MMHG_TO_PA:.17g}\n' for t in (0, 1, 2)))
                if file_pressure:
                    for condition in tree.getroot().findall('.//condition'):
                        condition.clear(); condition.attrib = {'type': 'pressure', 'subtype': 'file'}
                        ET.SubElement(condition, 'path', value=profile.name)
                tree.write(config)
                original = hashlib.sha256(config.read_bytes() + profile.read_bytes()).digest()
                output = work / f'v{version}-{file_pressure}'
                run(args.mpirun, args.hemelb, 2, config, output)
                actual = saved_sites(checkpoint_at(output, 4))
                assert expected.keys() == actual.keys()
                assert all(abs(a - b) <= 1e-12 for site in expected for a, b in zip(expected[site], actual[site]))
                assert original == hashlib.sha256(config.read_bytes() + profile.read_bytes()).digest()
                if file_pressure:
                    generated = next((output / 'Checkpoints').glob('*/restart.xml'))
                    assert all(c.get('units') == 'mmHg' for c in ET.parse(generated).findall('.//condition'))
                print(f'XML v{version}, pressure {"file" if file_pressure else "cosine"}: distributions match v6 within 1e-12; source unchanged')
        source = checkpoint_at(baseline, 2)
        source_offsets = baseline / 'Checkpoints/distributions.off'
        for version, float_values in ((4, True), (5, False), (5, True), (6, True), (7, True), (7, False)):
            label = f'xtr-v{version}-{"float" if float_values else "double"}'
            path = work / (label + '.xtr')
            decoded = make_legacy_checkpoint(source, source_offsets, path, version, float_values)
            config = work / (label + '.xml')
            make_config(config, path, path.with_suffix('.off'), changes={'simulation/steps': '2'})
            if version < 6:
                tree = legacy_xml(ET.parse(config), 3 if version == 4 else 5)
                if version == 4:
                    initial = tree.getroot().find('initialconditions')
                    cp = initial.find('checkpoint'); initial.remove(cp)
                    ET.SubElement(initial, 'pressure').append(cp)
                tree.write(config)
            output = work / label
            run(args.mpirun, args.hemelb, 4, config, output)
            assert saved_sites(checkpoint_at(output, 2)) == decoded
            assert ET.parse(output / 'report.xml').findtext('performance/completed_updates') == '0'
            print(f'{label}: 2-rank checkpoint loads on 4 ranks; all decoded distribution values match exactly')
        bad_origin = work / 'bad-legacy-origin.xml'
        make_config(bad_origin, work / 'xtr-v4-float.xtr', work / 'xtr-v4-float.off',
                    changes={'simulation/origin': '(-1e-05,-1.05e-05,-2.45248049736e-05)'})
        legacy_xml(ET.parse(bad_origin), 3).write(bad_origin)
        expect_rejected(args, bad_origin, work / 'bad-origin', 'Checkpoint was written with origin')
        bad_field = work / 'wrong-lattice.xtr'
        changed = bytearray((work / 'xtr-v5-double.xtr').read_bytes())
        struct.pack_into('>I', changed, 80, 16)
        bad_field.write_bytes(changed)
        shutil.copyfile(work / 'xtr-v5-double.off', bad_field.with_suffix('.off'))
        bad_lattice = work / 'bad-legacy-lattice.xml'
        make_config(bad_lattice, bad_field, bad_field.with_suffix('.off'))
        legacy_xml(ET.parse(bad_lattice), 5).write(bad_lattice)
        expect_rejected(args, bad_lattice, work / 'bad-lattice', 'but this build requires 15')
        absent_time = work / 'absent-time.xml'
        make_config(absent_time, work / 'xtr-v5-double.xtr', work / 'xtr-v5-double.off')
        tree = legacy_xml(ET.parse(absent_time), 5)
        ET.SubElement(tree.getroot().find('initialconditions'), 'time', units='lattice', value='99')
        tree.write(absent_time)
        expect_rejected(args, absent_time, work / 'absent-time', 'Target timestep 99 not found')
        absent_offsets = work / 'absent-offsets.xml'
        make_config(absent_offsets, work / 'xtr-v5-double.xtr', work / 'absent.off')
        legacy_xml(ET.parse(absent_offsets), 5).write(absent_offsets)
        expect_rejected(args, absent_offsets, work / 'absent-offsets', 'absent.off')
        print('Legacy checkpoints reject geometry/lattice mismatches, missing timesteps, and missing offsets across all ranks')


if __name__ == '__main__':
    main()
