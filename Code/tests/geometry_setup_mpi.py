#!/usr/bin/env python3
"""Verify decomposition modes, single-read geometry I/O, and performance formulas."""
import argparse
import math
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import xml.etree.ElementTree as ET

from checkpoint_restart_mpi import make_config, run, checkpoint_at, saved_sites


def verify_report(output, ranks, method, original):
    root = ET.parse(output / 'report.xml').getroot()
    perf = root.find('performance')
    sites = int(root.findtext('geometry/sites'))
    steps = int(perf.findtext('completed_updates'))
    seconds = float(perf.findtext('loop_seconds'))
    mlups = float(perf.findtext('mlups'))
    assert steps == 4
    assert sites == 5576
    assert perf.findtext('decomposition') == method
    assert seconds > 0
    assert math.isclose(mlups, sites * steps / (1e6 * seconds), rel_tol=1e-14)
    assert math.isclose(float(perf.findtext('mlups_per_rank')), mlups / ranks, rel_tol=1e-14)
    data = original.read_bytes()
    records = struct.iter_unpack('>III', data[32:272])
    metadata = list(records)
    assert int(perf.findtext('geometry_block_reads')) == sum(sites > 0 for sites, _, _ in metadata)
    assert int(perf.findtext('geometry_bytes_read')) == sum(length for _, length, _ in metadata)
    memories = perf.findall('rank_memory')
    assert len(memories) == ranks
    assert all(int(m.get('peak_rss_bytes')) >= int(m.get('setup_peak_rss_bytes')) > 0 for m in memories)
    assert float(perf.findtext('load_imbalance')) >= 1
    return {'ranks': ranks, 'method': method, 'sites': sites, 'steps': steps, 'loop_seconds': seconds,
            'mlups': mlups, 'load_imbalance': float(perf.findtext('load_imbalance')),
            'seconds_per_update': seconds / steps,
            'halo_distributions': [int(m.get('halo_distributions')) for m in memories],
            'halo_send_bytes': [int(m.get('halo_send_bytes')) for m in memories],
            'peak_rss_bytes': [int(m.get('peak_rss_bytes')) for m in memories]}



def verify_reference_pressure(work, launcher, executable):
    config = work / 'reference-pressure.xml'
    make_config(config)
    tree = ET.parse(config)
    root = tree.getroot()
    ET.SubElement(root.find('simulation'), 'reference_pressure', units='Pa', value='2')
    root.find('initialconditions/pressure/uniform').set('value', '3')
    for mean in root.findall('.//condition/mean'):
        mean.set('value', '3')
    prop = ET.SubElement(root.find('properties'), 'propertyoutput', file='pressure.xtr', period='1')
    ET.SubElement(prop, 'geometry', type='whole')
    ET.SubElement(prop, 'field', type='pressure', name='pressure')
    tree.write(config)
    output = work / 'reference-pressure'
    run(launcher, executable, 2, config, output)
    data = (output / 'Extracted/pressure.xtr').read_bytes()
    count = struct.unpack_from('>Q', data, 68)[0]
    header_length = struct.unpack_from('>I', data, 80)[0]
    name_length = struct.unpack_from('>I', data, 84)[0]
    field_start = 88 + ((name_length + 3) // 4) * 4
    components, typecode, offsets = struct.unpack_from('>III', data, field_start)
    assert (components, typecode, offsets) == (1, 0, 1)
    offset, scale = struct.unpack_from('>ff', data, field_start + 12)
    assert math.isclose(offset * scale, 2, rel_tol=1e-6)
    body = 84 + header_length + 8
    for i in range(count):
        lattice_pressure = struct.unpack_from('>f', data, body + i * 16 + 12)[0]
        assert math.isclose((lattice_pressure + offset) * scale, 3, abs_tol=1e-5)
    print('Nonzero reference pressure: decoded physical pressure is 3 Pa')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hemelb', required=True)
    parser.add_argument('--mpirun', required=True)
    parser.add_argument('--results', type=Path)
    args = parser.parse_args()
    original = Path(__file__).parent / 'resources/large_cylinder.gmy'
    records = []
    with tempfile.TemporaryDirectory(prefix='geometry-setup-') as directory:
        work = Path(directory)
        shutil.copyfile(original, work / original.name)
        verify_reference_pressure(work, args.mpirun, args.hemelb)
        for ranks in (1, 2, 4):
            states = {}
            for method in ('octree', 'parmetis'):
                config = work / f'{method}-{ranks}.xml'
                make_config(config)
                tree = ET.parse(config)
                ET.SubElement(tree.getroot(), 'decomposition', method=method)
                tree.write(config)
                output = work / f'{method}-{ranks}'
                run(args.mpirun, args.hemelb, ranks, config, output)
                records.append(verify_report(output, ranks, method, original))
                states[method] = saved_sites(checkpoint_at(output, 4))
            assert states['octree'].keys() == states['parmetis'].keys()
            for key in states['octree']:
                assert all(abs(a - b) <= 1e-12 for a, b in zip(states['octree'][key], states['parmetis'][key]))
            print(f'{ranks} ranks: both modes preserve sites, distributions agree within 1e-12, geometry read once, report formulas agree')
    if args.results:
        import json
        args.results.write_text(json.dumps(records, indent=2) + '\n')


if __name__ == '__main__':
    main()
