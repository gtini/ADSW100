#!/usr/bin/env python3
"""Fondazione SDCP2 offline: nessun accesso a firmware, EEPROM fisica o rete."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

ROOT = Path(__file__).resolve().parent
# Codici wire congelati: aggiungere un tipo richiede una revisione esplicita.
TYPES = ['opaque', 'u8', 'u16', 'u32', 'i8', 'i16', 'i32', 'bool', 'ascii',
         'ipv4', 'ipv4_prefix', 'route']
SELECTORS = ['none', 'u8', 'u32']
UNITS = ['none', 'second', 'minute', 'hertz', 'level']
WIDTH = {'u8': 1, 'u16': 2, 'u32': 4, 'i8': 1, 'i16': 2, 'i32': 4,
         'bool': 1, 'ipv4': 4, 'ipv4_prefix': 5, 'route': 8}
MAX_CATALOG = 65536
MAX_SDCP = 4096


def require(ok, message):
    if not ok:
        raise ValueError(message)


def pairs(items):
    out = {}
    for k, v in items:
        require(k not in out, 'JSON: chiave duplicata ' + k)
        out[k] = v
    return out


def load(path):
    def invalid(value):
        raise ValueError('JSON: numero non finito ' + value)
    return json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=pairs,
                      parse_constant=invalid)


def schema_check(data, kind):
    """Valida il sottoinsieme dichiarato da schema.json; nessuna dipendenza esterna."""
    schema = load(ROOT / 'schema.json')
    def check(v, rule, where):
        if '$ref' in rule:
            return check(v, schema['$defs'][rule['$ref'].split('/')[-1]], where)
        if 'const' in rule:
            require(type(v) is type(rule['const']) and v == rule['const'], where + ': const')
        if 'enum' in rule:
            require(v in rule['enum'], where + ': enum')
        if 'type' in rule:
            valid = {'object': type(v) is dict, 'array': type(v) is list,
                     'integer': type(v) is int, 'string': type(v) is str}
            require(valid[rule['type']], where + ': tipo')
        if type(v) is dict:
            props = rule.get('properties', {})
            require(all(k in v for k in rule.get('required', [])), where + ': campo mancante')
            if rule.get('additionalProperties') is False:
                require(set(v) <= set(props), where + ': campo sconosciuto')
            for k in v:
                if k in props:
                    check(v[k], props[k], where + '.' + k)
        elif type(v) is list:
            require(rule.get('minItems', 0) <= len(v) <= rule.get('maxItems', 2**32), where + ': elementi')
            for i, item in enumerate(v):
                check(item, rule['items'], f'{where}[{i}]')
        elif type(v) is str:
            require(rule.get('minLength', 0) <= len(v) <= rule.get('maxLength', 2**32), where + ': lunghezza')
            require('pattern' not in rule or re.fullmatch(rule['pattern'], v) is not None, where + ': pattern')
        elif type(v) is int:
            require(rule.get('minimum', v) <= v <= rule.get('maximum', v), where + ': range')
    check(data, schema['$defs'][kind], kind)


def key_parts(key):
    fields = key.split(':')
    if fields[0] == 'std':
        return 0, 0, int(fields[1], 16)
    return 1, int(fields[1], 16), int(fields[2], 16)


def ascii_bytes(text):
    require(isinstance(text, str) and 1 <= len(text) <= 63 and
            all(32 <= ord(c) <= 126 for c in text), 'Testo ASCII stampabile 1..63 richiesto')
    return text.encode('ascii')


def lstring(text):
    b = ascii_bytes(text)
    return bytes([len(b)]) + b


def integer_bytes(value, typ):
    require(type(value) is int, 'Intero richiesto')
    signed = typ in ('i8', 'i16', 'i32')
    bits = WIDTH[typ] * 8
    require(-(1 << (bits - 1)) <= value < (1 << (bits - 1)) if signed else
            0 <= value < (1 << bits), 'Intero non rappresentabile')
    return value.to_bytes(WIDTH[typ], 'big', signed=typ.startswith('i') and typ not in ('ipv4', 'ipv4_prefix'))


def valid_ip(b):
    return b == b'\0' * 4 or (0 < b[0] < 224 and b[0] != 127)


def check_value(p, value):
    typ = p['type']
    require(type(value) is bytes and len(value) <= 64, 'Valore oltre limite')
    if typ in WIDTH:
        require(len(value) == WIDTH[typ], 'Dimensione valore errata')
    if typ in ('u8', 'u16', 'u32', 'i8', 'i16', 'i32', 'bool'):
        n = int.from_bytes(value, 'big', signed=typ.startswith('i'))
        require(p['minimum'] <= n <= p['maximum'], 'Valore fuori range')
        if typ == 'bool':
            require(n in (0, 1), 'Booleano invalido')
        if p['choices']:
            require(n in [c['value'] for c in p['choices']], 'Enum sconosciuto')
    elif typ == 'ascii':
        require(p['minimum'] <= len(value) <= p['maximum'] and
                all(32 <= x <= 126 for x in value), 'ASCII invalido')
    elif typ in ('ipv4', 'ipv4_prefix'):
        require(valid_ip(value[:4]), 'IPv4 invalido')
        if typ == 'ipv4_prefix':
            require(p['minimum'] <= value[4] <= p['maximum'] <= 32, 'Prefisso invalido')
    elif typ == 'route':
        # ADSW conserva un gateway Ethernet; forma canonica del SET route ID=0.
        require(value[:4] == b'\0' * 4 and valid_ip(value[4:]), 'Route non rappresentabile')
    else:
        raise ValueError('Tipo non editabile')


def registry_check(registry):
    schema_check(registry, 'registry')
    keys = set()
    for p in registry['parameters']:
        require(p['key'] not in keys, 'PID duplicato nello stesso namespace')
        keys.add(p['key'])
        ns, man, pid = key_parts(p['key'])
        require((ns == 0 and 0 < pid < 0x8000) or
                (ns == 1 and man not in (0, 0xFFFF) and 0x8000 <= pid <= 0xFFDF), 'Namespace PID invalido')
        ascii_bytes(p['name']); ascii_bytes(p['semantic'])
        require(p['minimum'] <= p['maximum'], 'Range registro invertito')
        enum_check(p)
    return {p['key']: p for p in registry['parameters']}


def enum_check(p):
    if p['type'] in ('u8', 'u16', 'u32', 'i8', 'i16', 'i32', 'bool'):
        integer_bytes(p['minimum'], p['type']); integer_bytes(p['maximum'], p['type'])
    if p['type'] == 'bool':
        require(0 <= p['minimum'] <= p['maximum'] <= 1, 'Range booleano')
    if p['type'] == 'ascii':
        require(0 <= p['minimum'] <= p['maximum'] <= 64, 'Limite ASCII')
    values = [c['value'] for c in p['choices']]
    require(len(values) == len(set(values)), 'Enum duplicato')
    require(not values or p['type'] in ('u8', 'u16', 'u32', 'i8', 'i16', 'i32', 'bool'), 'Enum non numerico')
    for c in p['choices']:
        ascii_bytes(c['label'])
        require(p['minimum'] <= c['value'] <= p['maximum'], 'Enum fuori range')
        integer_bytes(c['value'], p['type'])


def compile_model(registry, catalog):
    lookup = registry_check(registry)
    schema_check(catalog, 'catalog')
    man, model = int(catalog['manufacturer'], 16), int(catalog['model'], 16)
    require(man not in (0, 0xFFFF), 'Manufacturer invalido')
    params = []
    seen = set()
    for c in catalog['parameters']:
        key = c['key']
        require(key in lookup and key not in seen, 'PID non registrato o duplicato')
        seen.add(key)
        r = lookup[key]
        require(r['type'] != 'opaque', 'Contratto NFC non definito per PID')
        require(key_parts(key)[1] in (0, man), 'Manufacturer parametro diverso dal modello')
        require(r['minimum'] <= c['minimum'] <= c['maximum'] <= r['maximum'], 'Range modello incompatibile')
        p = {**r, **c}
        ascii_bytes(p['binding']); ascii_bytes(p['label'])
        enum_check(p)
        base_enum = {x['value']: x['label'] for x in r['choices']}
        if base_enum:
            require(all(base_enum.get(x['value']) == x['label'] for x in p['choices']) and p['choices'], 'Enum ridefinito')
        sel = p['selectors']
        require(len(sel) == len(set(sel)), 'Selettore duplicato')
        require(p['selector'] != 'none' or sel == [0], 'Selettore su parametro scalare')
        limit = 255 if p['selector'] == 'u8' else 0xFFFFFFFF
        require(all(0 <= x <= limit for x in sel), 'Selettore fuori range')
        p['selectors'] = sorted(sel)
        check_value(p, bytes.fromhex(p['default']))
        p['choices'] = sorted(p['choices'], key=lambda x: x['value'])
        params.append(p)
    params.sort(key=lambda p: key_parts(p['key']))
    require(sum(len(p['selectors']) for p in params) <= 64, 'Troppe istanze')
    return {'manufacturer': man, 'model': model, 'revision': catalog['schema_revision'],
            'name': catalog['name'], 'category': int(catalog['category'], 16), 'parameters': params}


def contract(model):
    result = struct.pack('>HH', len(model['parameters']), model['category'])
    for p in model['parameters']:
        ns, man, pid = key_parts(p['key'])
        raw = struct.pack('>BHHBBBBB', ns, man, pid, TYPES.index(p['type']),
                          SELECTORS.index(p['selector']), int(p['access'] == 'rw'), UNITS.index(p['unit']), len(p['selectors']))
        width = 0 if p['selector'] == 'none' else 1 if p['selector'] == 'u8' else 4
        raw += b''.join(x.to_bytes(width, 'big') if width else b'' for x in p['selectors'])
        raw += struct.pack('>qq', p['minimum'], p['maximum'])
        default = bytes.fromhex(p['default'])
        raw += bytes([len(default)]) + default + bytes([len(p['choices'])])
        raw += b''.join(integer_bytes(c['value'], p['type']) for c in p['choices'])
        raw += lstring(p['semantic']) + lstring(p['binding'])
        result += struct.pack('>H', len(raw)) + raw
    return result


def fingerprint(model):
    return hashlib.sha256(b'SDCP2-SCHEMA\0' + struct.pack('>HHI', model['manufacturer'],
                          model['model'], model['revision']) + contract(model)).digest()


def catalog_binary(model):
    c = contract(model)
    labels = lstring(model['name'])
    for p in model['parameters']:
        labels += lstring(p['label']) + b''.join(lstring(x['label']) for x in p['choices'])
    length = 60 + len(c) + len(labels) + 4
    require(length <= MAX_CATALOG, 'Catalogo troppo grande')
    header = struct.pack('>4sHHIHHI32sII', b'SDC2', 1, 0, length, model['manufacturer'],
                         model['model'], model['revision'], fingerprint(model), len(c), len(labels))
    raw = header + c + labels
    return raw + struct.pack('>I', zlib.crc32(raw))


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, size):
        require(0 <= size <= len(self.data) - self.pos, 'Dati troncati')
        result = self.data[self.pos:self.pos + size]
        self.pos += size
        return result

    def unpack(self, fmt):
        return struct.unpack(fmt, self.take(struct.calcsize(fmt)))

    def byte(self):
        return self.take(1)[0]

    def text(self):
        s = self.take(self.byte()).decode('ascii')
        ascii_bytes(s)
        return s

    def done(self):
        require(self.pos == len(self.data), 'Dati residui')


def decode_catalog(data):
    require(64 <= len(data) <= MAX_CATALOG, 'Lunghezza catalogo')
    require(zlib.crc32(data[:-4]) == int.from_bytes(data[-4:], 'big'), 'CRC catalogo')
    h = Reader(data)
    magic, version, flags, total, man, mod, rev, digest, clen, plen = h.unpack('>4sHHIHHI32sII')
    require((magic, version, flags, total) == (b'SDC2', 1, 0, len(data)) and man not in (0, 65535) and rev, 'Header catalogo')
    c, labels = Reader(h.take(clen)), Reader(h.take(plen))
    h.take(4); h.done()
    count, category = c.unpack('>HH')
    require(1 <= count <= 64, 'Numero parametri')
    model = dict(manufacturer=man, model=mod, revision=rev, category=category, name=labels.text(), parameters=[])
    for _ in range(count):
        size, = c.unpack('>H'); r = Reader(c.take(size))
        ns, manufacturer, pid, typ, selector, access, unit, nsel = r.unpack('>BHHBBBBB')
        require(ns in (0, 1) and typ in range(1, len(TYPES)) and selector < 3 and access < 2 and unit < len(UNITS) and 1 <= nsel <= 16, 'Tipo/codice catalogo')
        require((ns == 0 and manufacturer == 0 and pid < 0x8000) or
                (ns == 1 and manufacturer == man and 0x8000 <= pid <= 0xFFDF), 'Chiave catalogo')
        key = f'std:{pid:04X}' if ns == 0 else f'mfr:{manufacturer:04X}:{pid:04X}'
        w = (0, 1, 4)[selector]
        sels = [int.from_bytes(r.take(w), 'big') for _ in range(nsel)]
        lo, hi = r.unpack('>qq'); default = r.take(r.byte())
        p = dict(key=key, type=TYPES[typ], selector=SELECTORS[selector], access='rw' if access else 'r', unit=UNITS[unit],
                 selectors=sels, minimum=lo, maximum=hi, default=default.hex().upper(), choices=[])
        nc = r.byte(); require(nc <= 64, 'Troppe scelte')
        require(not nc or p['type'] in ('u8', 'u16', 'u32', 'i8', 'i16', 'i32', 'bool'), 'Enum non scalare')
        for _ in range(nc):
            value = int.from_bytes(r.take(WIDTH[p['type']]), 'big', signed=p['type'].startswith('i'))
            p['choices'].append(dict(value=value, label='placeholder'))
        require([x['value'] for x in p['choices']] == sorted({x['value'] for x in p['choices']}), 'Ordine enum')
        p['semantic'], p['binding'] = r.text(), r.text(); r.done()
        p['label'] = labels.text()
        for choice in p['choices']:
            choice['label'] = labels.text()
        require(lo <= hi and sels == sorted(set(sels)) and (selector != 0 or sels == [0]), 'Range/selettori catalogo')
        enum_check(p); check_value(p, default)
        model['parameters'].append(p)
    c.done(); labels.done()
    keys = [key_parts(p['key']) for p in model['parameters']]
    require(keys == sorted(set(keys)), 'Ordine catalogo')
    require(sum(len(p['selectors']) for p in model['parameters']) <= 64, 'Troppe istanze')
    require(fingerprint(model) == digest, 'Fingerprint schema errato')
    require(catalog_binary(model) == data, 'Catalogo non canonico')
    return model


def instances(model):
    result = {}
    for p in model['parameters']:
        w = {'none': 0, 'u8': 1, 'u32': 4}[p['selector']]
        for n in p['selectors']:
            result[(key_parts(p['key'])[2], n.to_bytes(w, 'big') if w else b'')] = p
    return result


def default_values(model):
    return [(pid, sel, bytes.fromhex(p['default'])) for (pid, sel), p in sorted(instances(model).items())]


def section(number, data, flags=1):
    return struct.pack('>BBH', number, flags, len(data)) + data


def encode_sdcp(model, values=None, kind=0, revision=1, request=0, applied=0,
                device=1, software=0x020000):
    values = default_values(model) if values is None else values
    identity = struct.pack('>HHHIHI', model['manufacturer'], model['model'],
                           model['category'],
                           software, model['manufacturer'], device)
    val = b''.join(struct.pack('>HB', pid, len(sel)) + sel + bytes([len(v)]) + v for pid, sel, v in values)
    body = section(1, identity) + section(2, val) + section(7, struct.pack('>I', model['revision']) + fingerprint(model))
    body += section(0x80, struct.pack('>BBIII', 1, kind, revision, request, applied))
    raw = b'SD\x20\0' + struct.pack('>H', 10 + len(body)) + body
    result = raw + struct.pack('>I', zlib.crc32(raw))
    decode_sdcp(result, model)
    return result


def decode_sdcp(data, model=None):
    require(10 <= len(data) <= MAX_SDCP and data[:4] == b'SD\x20\0' and int.from_bytes(data[4:6], 'big') == len(data), 'Header SDCP2')
    require(zlib.crc32(data[:-4]) == int.from_bytes(data[-4:], 'big'), 'CRC SDCP2')
    r = Reader(data[6:-4]); sections = {}; previous = 0
    while r.pos < len(r.data):
        sid, flags, size = r.unpack('>BBH')
        require(sid > previous and flags in (0, 1) and len(sections) < 16, 'Sezioni SDCP2')
        b = r.take(size)
        require(sid in (1, 2, 7, 0x80) or not flags, 'Sezione critica ignota')
        if sid in (1, 2, 7, 0x80):
            require(flags == 1, 'Sezione obbligatoria non critica')
        sections[sid] = (flags, b); previous = sid
    require(all(x in sections for x in (1, 2, 7, 0x80)), 'Sezione mancante')
    identity, ref, sync = [sections[x][1] for x in (1, 7, 0x80)]
    require(len(identity) == 16 and len(ref) == 36 and len(sync) == 14, 'Dimensione sezione')
    man, mod, cat, sw, uidman, dev = struct.unpack('>HHHIHI', identity)
    require(man not in (0, 65535) and uidman == man and dev != 0xFFFFFFFF and int.from_bytes(ref[:4], 'big') != 0, 'Identita/schema')
    ver, kind, rev, req, applied = struct.unpack('>BBIII', sync)
    next_request = ((applied + 1) & 0xFFFFFFFF) or 1
    require(ver == 1 and kind in (0, 1) and rev != 0 and req == (applied if kind == 0 else next_request), 'SYNC1 invalida')
    vals = Reader(sections[2][1]); values = []
    while vals.pos < len(vals.data):
        pid, size = vals.unpack('>HB'); require(size in (0, 1, 4), 'Dimensione selettore')
        sel = vals.take(size); val = vals.take(vals.byte())
        require(0 < pid <= 0xFFDF and len(val) <= 64, 'Limite valore/PID')
        values.append((pid, sel, val))
        require(len(values) <= 64, 'Troppe istanze')
    keys = [(p, s) for p, s, v in values]
    require(keys and keys == sorted(set(keys)), 'Chiavi duplicate/non ordinate')
    if model:
        require((man, mod, cat) == (model['manufacturer'], model['model'], model['category']) and ref == struct.pack('>I', model['revision']) + fingerprint(model), 'Schema/destinatario non corrispondente')
        known = instances(model)
        require(set(keys) == set(known), 'Insieme parametri non corrispondente')
        for pid, sel, value in values:
            check_value(known[(pid, sel)], value)
    return dict(identity=identity, reference=ref, values=values, kind=kind, revision=rev,
                request=req, applied=applied, sections=sections)


def validate_transaction(request, snapshot, model):
    a, b = decode_sdcp(request, model), decode_sdcp(snapshot, model)
    require(a['kind'] == 1 and b['kind'] == 0 and a['identity'] == b['identity'] and
            a['revision'] == b['revision'] and a['applied'] == b['applied'], 'Base transazione errata')
    known = instances(model)
    for (pid, sel, value), (_, _, original) in zip(a['values'], b['values']):
        require(known[(pid, sel)]['access'] == 'rw' or value == original, 'Modifica read-only')
    for sid, section_data in b['sections'].items():
        if sid not in (2, 0x80):
            require(a['sections'].get(sid) == section_data, 'Sezione immutabile modificata')
    require(set(a['sections']) == set(b['sections']), 'Sezioni aggiunte/rimosse')
    return a


def ndef_tlv(payload, capacity=816):
    decode_sdcp(payload)
    sr = len(payload) <= 255
    rec = bytes([0xD2 if sr else 0xC2, 30]) + (bytes([len(payload)]) if sr else struct.pack('>I', len(payload)))
    rec += b'application/vnd.studiodue.sdcp' + payload
    out = b'\x03' + (bytes([len(rec)]) if len(rec) < 255 else b'\xFF' + struct.pack('>H', len(rec))) + rec + b'\xFE'
    require(len(out) <= capacity, 'Capacita NDEF superata')
    return out


def decode_ndef_tlv(data, capacity=816):
    require(len(data) <= capacity, 'Capacita NDEF superata')
    r = Reader(data)
    require(r.byte() == 3, 'TLV NDEF atteso')
    size = r.byte()
    if size == 255:
        size, = r.unpack('>H')
    record = Reader(r.take(size))
    require(r.byte() == 254 and all(x == 0 for x in r.take(len(r.data) - r.pos)), 'Terminatore/padding NDEF')
    flags, typelen = record.byte(), record.byte()
    require(flags in (0xD2, 0xC2) and typelen == 30, 'Record MIME singolo atteso')
    payloadlen = record.byte() if flags == 0xD2 else record.unpack('>I')[0]
    require(record.take(typelen) == b'application/vnd.studiodue.sdcp', 'MIME diverso')
    payload = record.take(payloadlen); record.done()
    decode_sdcp(payload)
    return payload


def ads_network_decode(image):
    """Modello host dei soli byte rete legacy; nessun I/O o migrazione."""
    require(len(image) >= 40 and image[:2] == b'\xA5\x5A' and image[3] in (0, 1), 'EEPROM non valida')
    def ip(offset):
        # EE_Put32 salva BE il numero raw lwIP little-endian di STM32.
        return int.from_bytes(image[offset:offset + 4], 'big').to_bytes(4, 'little')
    mask = int.from_bytes(ip(8), 'big')
    inverse = mask ^ 0xFFFFFFFF
    require(inverse & (inverse + 1) == 0, 'Maschera legacy non contigua')
    values = {(0x0703, b'\0\0\0\1'): bytes([image[3]]),
              (0x0706, b'\0\0\0\1'): ip(4) + bytes([bin(mask).count('1')]),
              (0x070A, b''): b'\0' * 4 + ip(12),
              (0x070B, b'\0'): ip(16), (0x070B, b'\1'): ip(20)}
    for (pid, _), value in values.items():
        check_value(dict(type={0x0703: 'bool', 0x0706: 'ipv4_prefix', 0x070A: 'route',
                               0x070B: 'ipv4'}[pid], minimum=0, maximum=32 if pid == 0x0706 else 1, choices=[]), value)
    return values


def ads_network_encode(image, values):
    """Patch host conservativa: cambia esclusivamente 0x03..0x17."""
    original = ads_network_decode(image)
    require(set(values) == set(original), 'Mapping rete incompleto')
    for (pid, _), value in values.items():
        check_value(dict(type={0x0703: 'bool', 0x0706: 'ipv4_prefix', 0x070A: 'route',
                               0x070B: 'ipv4'}[pid], minimum=0, maximum=32 if pid == 0x0706 else 1, choices=[]), value)
    candidate = bytearray(image)
    def put(offset, ip):
        candidate[offset:offset + 4] = int.from_bytes(ip, 'little').to_bytes(4, 'big')
    candidate[3] = values[(0x0703, b'\0\0\0\1')][0]
    static = values[(0x0706, b'\0\0\0\1')]
    require(len(static) == 5 and static[4] <= 32, 'Prefisso statico')
    put(4, static[:4]); prefix = static[4]
    put(8, ((0xFFFFFFFF << (32 - prefix)) & 0xFFFFFFFF).to_bytes(4, 'big'))
    route = values[(0x070A, b'')]
    require(len(route) == 8 and route[:4] == b'\0' * 4, 'ID route non supportato')
    put(12, route[4:]); put(16, values[(0x070B, b'\0')]); put(20, values[(0x070B, b'\1')])
    require(ads_network_decode(candidate) == values, 'Valori rete non rappresentabili')
    return bytes(candidate)


def canonical_json(value):
    # Sottoinsieme canonico: nessun float, Unicode, slash escaping o numero ambiguo.
    def check(v):
        if type(v) is dict:
            for k, item in v.items():
                require(type(k) is str and k.isascii(), 'Chiave manifest non ASCII'); check(item)
        elif type(v) is list:
            for item in v:
                check(item)
        elif type(v) is str:
            require(v.isascii(), 'Manifest non ASCII')
        else:
            require(type(v) is int and 0 <= v <= 2**32 - 1, 'Numero manifest invalido')
    check(value)
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=True).encode('ascii')


def signing_input(manifest):
    require(set(manifest) == {'format', 'release', 'key_id', 'files'} and manifest['format'] == 1,
            'Manifest non riconosciuto')
    require(type(manifest['release']) is int and 1 <= manifest['release'] <= 0xFFFFFFFF, 'Release manifest')
    require(re.fullmatch('[a-z0-9_-]{1,32}', manifest['key_id']) is not None, 'Key ID')
    paths = []
    for f in manifest['files']:
        require(set(f) == {'path', 'size', 'sha256'}, 'Record manifest')
        require(re.fullmatch('[A-Z0-9_/-]+[.]sdcat', f['path']) is not None and
                not f['path'].startswith('/') and '..' not in f['path'] and '//' not in f['path'], 'Path manifest')
        require(type(f['size']) is int and 64 <= f['size'] <= MAX_CATALOG and
                re.fullmatch('[0-9a-f]{64}', f['sha256']) is not None, 'Dimensione/hash manifest')
        paths.append(f['path'])
    require(1 <= len(paths) <= 256 and paths == sorted(set(paths)), 'Ordine/file manifest')
    return b'SDCP2-CATALOG-PACKAGE\0' + canonical_json(manifest)


def decode_manifest(data):
    require(type(data) is bytes and len(data) <= MAX_CATALOG, 'Dimensione manifest')
    manifest = json.loads(data.decode('ascii'), object_pairs_hook=pairs)
    signing_input(manifest)
    require(canonical_json(manifest) == data, 'Manifest non canonico')
    return manifest


def verify_package(manifest, signature, files, trusted_keys, verify):
    """Contratto per il futuro verificatore Ed25519; nessuna fiducia implicita."""
    message = signing_input(manifest)
    key = trusted_keys.get(manifest['key_id'])
    require(type(key) is bytes and len(key) == 32 and type(signature) is bytes and len(signature) == 64,
            'Chiave/firma mancante o ignota')
    require(callable(verify) and verify(key, message, signature) is True, 'Firma non verificata')
    require(set(files) == {f['path'] for f in manifest['files']}, 'Insieme file pacchetto')
    for f in manifest['files']:
        raw = files[f['path']]
        require(len(raw) == f['size'] and hashlib.sha256(raw).hexdigest() == f['sha256'], 'File alterato')
        decode_catalog(raw)
    return True


def generate(output, release=1, key_id='release-1'):
    output = Path(output); output.mkdir(parents=True, exist_ok=True)
    registry = load(ROOT / 'registry.json')
    files = []
    for path in sorted((ROOT / 'catalogs').glob('*.json')):
        model = compile_model(registry, load(path)); binary = catalog_binary(model)
        name = path.stem + '.sdcat'; (output / name).write_bytes(binary)
        files.append(dict(path=name, size=len(binary), sha256=hashlib.sha256(binary).hexdigest()))
        (output / (path.stem + '.fingerprint')).write_bytes((fingerprint(model).hex() + '\n').encode('ascii'))
    manifest = dict(format=1, release=release, key_id=key_id, files=files)
    (output / 'manifest.json').write_bytes(canonical_json(manifest))
    (output / 'signing-input.bin').write_bytes(signing_input(manifest))
    # Nessuna firma inventata o chiave privata: il risultato non e un pacchetto fidato.
    return manifest


def firmware_header(model):
    """Esporta dati dal catalogo congelato, senza introdurre binding di modello nel client."""
    rows = []
    for (pid, selector), p in sorted(instances(model).items()):
        rows.append('    {0x%04Xu, %du, %du, %du, 0x%08Xu},' %
                    (pid, len(selector), WIDTH[p['type']], TYPES.index(p['type']), int.from_bytes(selector, 'big')))
    return ('/* Generato da protocol/nfc/sdcp.py: non modificare manualmente. */\n'
            '#ifndef NFC_CATALOG_H\n#define NFC_CATALOG_H\n#include <stdint.h>\n'
            '#define NFC_SCHEMA_REVISION %du\n#define NFC_CATALOG_MANUFACTURER 0x%04Xu\n'
            '#define NFC_CATALOG_MODEL 0x%04Xu\n#define NFC_CATALOG_CATEGORY 0x%04Xu\n' %
            (model['revision'], model['manufacturer'], model['model'], model['category']) +
            'static const uint8_t nfc_schema_hash[32] =\n{' +
            ','.join('0x%02X' % x for x in fingerprint(model)) + '};\n'
            'typedef struct\n{\n    uint16_t pid;\n    uint8_t selector_size, value_size, type;\n    uint32_t selector;\n} nfc_field_t;\n'
            'static const nfc_field_t nfc_fields[] =\n{\n' + '\n'.join(rows) +
            '\n};\n#define NFC_FIELD_COUNT (sizeof nfc_fields / sizeof nfc_fields[0])\n#endif\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, help='Directory artefatti non firmati')
    parser.add_argument('--release', type=int, default=1)
    parser.add_argument('--key-id', default='release-1')
    parser.add_argument('--firmware-header', help='Header dati C opzionale')
    parser.add_argument('--model', default='ADSW100', help='Nome sorgente catalogo per header')
    args = parser.parse_args()
    if args.firmware_header:
        require(re.fullmatch('[A-Za-z0-9_]+', args.model) is not None, 'Nome modello')
        model = compile_model(load(ROOT / 'registry.json'), load(ROOT / 'catalogs' / (args.model + '.json')))
        Path(args.firmware_header).write_bytes(firmware_header(model).encode('ascii'))
    for item in generate(args.output, args.release, args.key_id)['files']:
        print(item['path'], item['size'], item['sha256'])
