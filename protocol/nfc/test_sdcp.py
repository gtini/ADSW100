"""Regressioni host indipendenti dai firmware e da librerie installate."""
import copy
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
import sdcp as s


def wire(sections):
    body = b''.join(s.section(sid, data, flags) for sid, flags, data in sections)
    raw = b'SD\x20\0' + struct.pack('>H', len(body) + 10) + body
    return raw + struct.pack('>I', zlib.crc32(raw))


def changed_values(raw, values):
    d = s.decode_sdcp(raw)
    encoded = b''.join(struct.pack('>HB', pid, len(sel)) + sel + bytes([len(v)]) + v for pid, sel, v in values)
    return wire([(sid, flags, encoded if sid == 2 else value) for sid, (flags, value) in d['sections'].items()])


def vector_set():
    registry = s.load(s.ROOT / 'registry.json'); vectors = []
    def add(name, model_name, data, valid, reason=''):
        item = dict(name=name, model=model_name, valid=valid, reason=reason, sdcp=data.hex())
        if valid:
            item['ndef_tlv'] = s.ndef_tlv(data).hex()
        vectors.append(item)
    for path in sorted((s.ROOT / 'catalogs').glob('*.json')):
        model = s.compile_model(registry, s.load(path)); name = path.stem
        snapshot = s.encode_sdcp(model)
        vals = s.default_values(model)
        vals = [(pid, sel, b'\0\2' if pid == 0x00F0 else v) for pid, sel, v in vals]
        request = s.encode_sdcp(model, vals, kind=1, request=1)
        ack = s.encode_sdcp(model, vals, revision=2, request=1, applied=1)
        for kind, raw in [('snapshot', snapshot), ('request', request), ('ack', ack)]:
            add(name + '_' + kind, name, raw, True)
        add(name + '_crc', name, snapshot[:-1] + bytes([snapshot[-1] ^ 1]), False, 'CRC')
        add(name + '_truncated', name, snapshot[:-1], False, 'Header')
        parsed = s.decode_sdcp(snapshot)
        sections = [(sid, f, value) for sid, (f, value) in parsed['sections'].items()]
        add(name + '_missing_schema', name, wire([x for x in sections if x[0] != 7]), False, 'mancante')
        add(name + '_critical_unknown', name, wire(sorted(sections + [(0x7F, 1, b'') ])), False, 'critica ignota')
        add(name + '_optional_unknown', name, wire(sorted(sections + [(0x7F, 0, b'test')])), True)
        wrong = [(sid, f, value[:-1] + bytes([value[-1] ^ 2]) if sid == 7 else value) for sid, f, value in sections]
        add(name + '_schema_revision', name, wire(wrong), False, 'Schema')
        add(name + '_duplicate_key', name, changed_values(snapshot, [parsed['values'][0]] + parsed['values']), False, 'duplicate')
        add(name + '_unordered', name, changed_values(snapshot, list(reversed(parsed['values']))), False, 'ordinate')
        wrong = [(pid, sel, b'\x02\x01' if pid == 0x00F0 else v) for pid, sel, v in parsed['values']]
        add(name + '_address_513', name, changed_values(snapshot, wrong), False, 'range')
        wrongsync = [(sid, f, struct.pack('>BBIII', 1, 1, 1, 2, 0) if sid == 128 else value) for sid, f, value in sections]
        add(name + '_request_id', name, wire(wrongsync), False, 'SYNC1')
        if name == 'ADSW100':
            for suffix, target, replacement, reason in [('dhcp_2', 0x0703, b'\2', 'range'),
                    ('prefix_33', 0x0706, b'\xC0\xA8\1\x14\x21', 'Prefisso'),
                    ('multicast_ip', 0x0706, b'\xE0\0\0\1\x18', 'IPv4'),
                    ('route_id_1', 0x070A, b'\0\0\0\1\xC0\xA8\1\1', 'Route')]:
                wrong = [(pid, sel, replacement if pid == target else v) for pid, sel, v in parsed['values']]
                add(name + '_' + suffix, name, changed_values(snapshot, wrong), False, reason)
            wrong = [(pid, b'\2' if pid == 0x070B and sel == b'\1' else sel, v) for pid, sel, v in parsed['values']]
            add(name + '_dns_index', name, changed_values(snapshot, wrong), False, 'Insieme')
    return dict(format=1, vectors=vectors)


class Foundation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.registry = s.load(s.ROOT / 'registry.json')
        cls.sources = {p.stem: s.load(p) for p in (s.ROOT / 'catalogs').glob('*.json')}
        cls.models = {n: s.compile_model(cls.registry, c) for n, c in cls.sources.items()}

    def test_catalog_roundtrip_and_frozen_vectors(self):
        expected = s.load(s.ROOT / 'vectors/vectors.json')
        self.assertEqual(expected, vector_set())
        for v in expected['vectors']:
            with self.subTest(vector=v['name']):
                raw = bytes.fromhex(v['sdcp']); model = self.models[v['model']]
                if v['valid']:
                    s.decode_sdcp(raw, model)
                    self.assertEqual(s.decode_ndef_tlv(bytes.fromhex(v['ndef_tlv'])), raw)
                else:
                    with self.assertRaisesRegex(ValueError, v['reason']): s.decode_sdcp(raw, model)


    def test_json_strict_and_semantic_errors(self):
        c = self.sources['ADSW100']
        for mutate in [lambda x: x.update(unknown=1), lambda x: x.update(schema_revision=True),
                       lambda x: x['parameters'].append(x['parameters'][0]),
                       lambda x: x['parameters'][0].update(minimum=-1),
                       lambda x: x['parameters'][0].update(key='mfr:0206:8001'),
                       lambda x: x['parameters'][0].update(default='FFFF')]:
            x = copy.deepcopy(c); mutate(x)
            with self.assertRaises((ValueError, OverflowError)): s.compile_model(self.registry, x)
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'bad.json'; p.write_text('{"kind":1,"kind":2}')
            with self.assertRaises(ValueError): s.load(p)
        r = copy.deepcopy(self.registry); r['parameters'].append(r['parameters'][0])
        with self.assertRaises(ValueError): s.registry_check(r)

    def test_manual_schema_revision(self):
        original = self.models['ADSW100']
        changed = copy.deepcopy(original)
        changed['name'] = 'Altro nome'
        changed['parameters'][0]['label'] = 'Descrizione aggiornata'
        snapshot = s.encode_sdcp(original)
        s.decode_sdcp(snapshot, changed)
        # Nessun controllo automatico del contratto: la responsabilita' della
        # revisione e' dell'autore. La revisione differente impedisce REQUEST.
        changed['revision'] += 1
        with self.assertRaises(ValueError): s.decode_sdcp(snapshot, changed)
        with self.assertRaises(ValueError):
            s.validate_transaction(s.encode_sdcp(changed, kind=1, request=1), snapshot, changed)
        parsed = s.decode_sdcp(snapshot)
        sections = [(sid, f, v + bytes(32) if sid == 7 else v)
                    for sid, (f, v) in parsed['sections'].items()]
        with self.assertRaises(ValueError): s.decode_sdcp(wire(sections))

    def test_namespaces_not_globally_unique(self):
        r = copy.deepcopy(self.registry)
        p = copy.deepcopy(next(p for p in r['parameters'] if p['key'] == 'mfr:0206:8001'))
        p['key'] = 'mfr:17FA:8001'; p['semantic'] = 'different_test_semantic'
        r['parameters'].append(p)
        s.registry_check(r)  # Stesso numero, manufacturer distinto: consentito.
        r['parameters'].append(copy.deepcopy(p))
        with self.assertRaises(ValueError): s.registry_check(r)

    def test_transaction_and_wrap(self):
        for m in self.models.values():
            snap = s.encode_sdcp(m)
            req = s.encode_sdcp(m, kind=1, request=1)
            s.validate_transaction(req, snap, m)
            with self.assertRaises(ValueError): s.validate_transaction(s.encode_sdcp(m, kind=1, revision=2, request=1), snap, m)
            # Il parser supporta il wrap di request_id senza riusare zero.
            snap = s.encode_sdcp(m, request=0xFFFFFFFF, applied=0xFFFFFFFF)
            req = s.encode_sdcp(m, kind=1, request=1, applied=0xFFFFFFFF)
            s.validate_transaction(req, snap, m)
            m = copy.deepcopy(m); m['parameters'][0]['access'] = 'r'
            vals = s.default_values(m); pid, sel, old = vals[0]
            vals[0] = (pid, sel, (int.from_bytes(old, 'big') + 1).to_bytes(len(old), 'big'))
            with self.assertRaises(ValueError):
                s.validate_transaction(s.encode_sdcp(m, vals, kind=1, request=1), s.encode_sdcp(m), m)

    def test_ndef_and_independent_offsets(self):
        for name, m in self.models.items():
            raw = s.encode_sdcp(m); t = s.ndef_tlv(raw)
            self.assertEqual(len(raw), 165 if name == 'ADSW100' else 170)
            self.assertEqual(len(t), 201 if name == 'ADSW100' else 206)
            self.assertEqual(s.decode_ndef_tlv(t), raw)
            self.assertEqual(s.decode_ndef_tlv(t + b'\0' * (240 - len(t))), raw)
            self.assertEqual(raw[:10], b'SD\x20\0' + struct.pack('>H', len(raw)) + b'\1\1\0\x10')
            self.assertEqual(raw[10:14], struct.pack('>HH', m['manufacturer'], m['model']))
            self.assertEqual(raw[-18:-4], struct.pack('>BBIII', 1, 0, 1, 0, 0))
            with self.assertRaises(ValueError): s.ndef_tlv(raw, capacity=100)

    def test_ndef_extended_and_malformed(self):
        raw = s.encode_sdcp(self.models['ADSW100']); d = s.decode_sdcp(raw)
        sections = [(sid, f, val) for sid, (f, val) in d['sections'].items()]
        for extra in (0, 24, 25, 58, 59, 60, 500):
            extended = wire(sorted(sections + [(127, 0, b'X' * extra)]))
            self.assertEqual(s.decode_ndef_tlv(s.ndef_tlv(extended)), extended)
        wrapped = s.ndef_tlv(raw)
        for i in (0, 1, 2, 3, 4, len(wrapped) - 1):
            broken = bytearray(wrapped); broken[i] ^= 1
            with self.assertRaises(ValueError): s.decode_ndef_tlv(bytes(broken))


    def test_wire_limits_and_unknown_versions(self):
        raw = s.encode_sdcp(self.models['ADSW100'])
        for n in range(len(raw)):
            with self.assertRaises(ValueError): s.decode_sdcp(raw[:n])
        for offset, value in ((2,16),(3,1),(7,0),(8,255)):
            broken = bytearray(raw[:-4]); broken[offset] = value
            broken += struct.pack('>I', zlib.crc32(broken))
            with self.assertRaises(ValueError): s.decode_sdcp(bytes(broken))

    def test_pwm_one_byte_enum(self):
        p = next(p for p in self.models['DRV_STG4']['parameters'] if p['key'] == 'mfr:0206:8200')
        self.assertEqual(p['type'], 'u8')
        self.assertEqual(p['choices'], [dict(value=i, label=f'F{i+1}') for i in range(7)])
        for i in range(7): s.check_value(p, bytes([i]))
        with self.assertRaises(ValueError): s.check_value(p, b'\7')
        with self.assertRaises(ValueError): s.check_value(p, b'\x03\xE8')

    def test_network_legacy_roundtrip(self):
        image = bytearray((n * 37 + 11) & 255 for n in range(128)); image[:3] = b'\xA5\x5A\3'; image[3] = 1
        def ip(offset, b): image[offset:offset + 4] = int.from_bytes(bytes(b), 'little').to_bytes(4, 'big')
        for off, val in [(4, [192,168,1,20]), (8, [255,255,255,0]), (12,[192,168,1,1]), (16,[8,8,8,8]), (20,[1,1,1,1])]: ip(off, val)
        values = s.ads_network_decode(image)
        self.assertEqual(values[(0x0706, b'\0\0\0\1')], bytes([192,168,1,20,24]))
        self.assertEqual(image[4:8], bytes([20,1,168,192]))
        self.assertEqual(s.ads_network_encode(image, values), bytes(image))
        for prefix in range(33):
            candidate = dict(values); candidate[(0x0706, b'\0\0\0\1')] = bytes([10,20,30,40,prefix])
            patched = s.ads_network_encode(image, candidate)
            self.assertEqual(s.ads_network_decode(patched), candidate)
            self.assertEqual(patched[:3], image[:3]); self.assertEqual(patched[24:], image[24:])
            self.assertEqual(patched[3], 1)  # Non spegnere DHCP per un valore statico conservato.
        ip(8, [255,0,255,0])
        with self.assertRaises(ValueError): s.ads_network_decode(image)




if __name__ == '__main__':
    unittest.main(verbosity=2)
