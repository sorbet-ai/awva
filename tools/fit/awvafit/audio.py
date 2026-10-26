# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Audio in and out: AIFF and WAV read here (no aifc: Python 3.13 removed it), FLAC through soundfile
or ffmpeg, raw float32 (the renderer's), resampling, and WAV writing for listening packs.

Recordings are data: nothing in them is run. Reading a file only parses its chunks.

  read(path, dtype=np.float32)       -> (mono x, sample rate): int PCM / full scale, channels averaged
  read_channels(path)                -> (ints [n, ch], sample rate, bits) of a PCM AIFF or WAV
  decode_ffmpeg(path, sr, cache)     -> mono float32 at sr, as ffmpeg resamples and mixes it down
  resample(x, sr_in, sr_out)         -> polyphase (scipy.signal.resample_poly), the reduced ratio
  read_f32 / write_f32               raw float32 (awva_render --out)
  write_wav(path, x, sr, bits=16|24|32, scale=1.0)
"""
import math
import os
import struct
import subprocess

import numpy as np

from . import env


class AudioError(ValueError):
    pass


def _ieee_extended(b):
    """an AIFF COMM sample rate: 80-bit IEEE 754 extended, big-endian"""
    exp = ((b[0] & 0x7f) << 8) | b[1]
    mant = int.from_bytes(b[2:10], 'big')
    if exp == 0 and mant == 0:
        return 0.0
    sign = -1.0 if b[0] & 0x80 else 1.0
    return sign * mant * 2.0 ** (exp - 16383 - 63)


def _pcm(raw, bits, ch, big):
    if bits == 16:
        a = np.frombuffer(raw, '>i2' if big else '<i2')
    elif bits == 24:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
        v = (b[:, 0] << 16 | b[:, 1] << 8 | b[:, 2]) if big else (b[:, 0] | b[:, 1] << 8 | b[:, 2] << 16)
        a = np.where(v >= 1 << 23, v - (1 << 24), v)
    elif bits == 32:
        a = np.frombuffer(raw, '>i4' if big else '<i4')
    elif bits == 8:
        a = np.frombuffer(raw, np.int8)
    else:
        raise AudioError(f'{bits}-bit PCM is not read here')
    n = len(a) // ch
    return a[:n * ch].reshape(n, ch)


def _aiff(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'FORM' or data[8:12] not in (b'AIFF', b'AIFC'):
        raise AudioError(f'{path}: not an AIFF file')
    pos, comm, ssnd, big = 12, None, None, True
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack('>I', data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b'COMM':
            ch, nfr, bits = struct.unpack('>hIh', body[:8])
            sr = _ieee_extended(body[8:18])
            if data[8:12] == b'AIFC' and len(body) >= 22:
                comp = body[18:22]
                if comp == b'sowt':
                    big = False
                elif comp not in (b'NONE', b'twos'):
                    raise AudioError(f'{path}: compressed AIFF-C ({comp!r}) is not read here')
            comm = (ch, nfr, bits, sr)
        elif cid == b'SSND':
            off = struct.unpack('>I', body[:4])[0]
            ssnd = body[8 + off:]
        pos += 8 + size + (size & 1)
    if comm is None or ssnd is None:
        raise AudioError(f'{path}: no COMM or SSND chunk')
    ch, nfr, bits, sr = comm
    nb = (bits + 7) // 8
    return _pcm(ssnd[:nfr * ch * nb], nb * 8, ch, big), int(round(sr)), nb * 8


def _wav(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        raise AudioError(f'{path}: not a WAV file')
    pos, fmt, raw = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack('<I', data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b'fmt ':
            tag, ch, sr = struct.unpack('<HHI', body[:8])
            bits = struct.unpack('<H', body[14:16])[0]
            if tag == 0xFFFE and len(body) >= 26:
                tag = struct.unpack('<H', body[24:26])[0]
            fmt = (tag, ch, sr, bits)
        elif cid == b'data':
            raw = body
        pos += 8 + size + (size & 1)
    if fmt is None or raw is None:
        raise AudioError(f'{path}: no fmt or data chunk')
    tag, ch, sr, bits = fmt
    if tag == 3:
        a = np.frombuffer(raw[:len(raw) // (4 * ch) * 4 * ch], '<f4' if bits == 32 else '<f8').reshape(-1, ch)
        return a, sr, -bits  # float: negative bits
    if tag != 1:
        raise AudioError(f'{path}: WAV format {tag} is not read here')
    if bits == 8:
        a = (np.frombuffer(raw, np.uint8).astype(np.int16) - 128).reshape(-1, ch)
        return a, sr, 8
    return _pcm(raw, bits, ch, False), sr, bits


def read_channels(path):
    """(samples [n, channels], sample rate, bits) of an AIFF or WAV: ints for PCM, floats for float
    WAV (bits negative)"""
    with open(path, 'rb') as f:
        head = f.read(12)
    if head[:4] == b'FORM':
        return _aiff(path)
    if head[:4] == b'RIFF':
        return _wav(path)
    raise AudioError(f'{path}: neither AIFF nor WAV (FLAC and others: decode_ffmpeg or read_soundfile)')


def read(path, dtype=np.float32):
    """mono samples at full scale 1.0 (PCM / 2^(bits-1), the channels averaged) and the rate"""
    a, sr, bits = read_channels(path)
    x = a.astype(dtype) if bits < 0 else a.astype(dtype) / dtype(2 ** (bits - 1))
    return x.reshape(-1, a.shape[1]).mean(1).astype(dtype, copy=False), sr


def read_soundfile(path, dtype='float32'):
    """any format libsndfile reads (FLAC, OGG, ...), mono; needs the soundfile package"""
    import soundfile  # BSD-3; its wheels bundle libsndfile (LGPL-2.1)
    x, sr = soundfile.read(path, dtype=dtype, always_2d=True)
    return x.mean(1).astype(dtype, copy=False), sr


def decode_ffmpeg(path, sr=48000, cache=None):
    """mono float32 at sr as ffmpeg decodes, mixes down and resamples it (its default resampler);
    with cache: the decoded file is kept there and read back next time"""
    if cache and os.path.exists(cache):
        return np.fromfile(cache, np.float32)
    cmd = [env.ffmpeg(), '-nostdin', '-loglevel', 'error', '-y', '-i', path, '-ac', '1', '-ar', str(sr), '-f', 'f32le']
    if cache:
        os.makedirs(os.path.dirname(cache) or '.', exist_ok=True)
        subprocess.run(cmd + [cache], check=True)
        return np.fromfile(cache, np.float32)
    r = subprocess.run(cmd + ['-'], check=True, capture_output=True)
    return np.frombuffer(r.stdout, np.float32).copy()


def resample(x, sr_in, sr_out):
    """polyphase resampling by the reduced ratio (44.1 -> 48 kHz: up 160, down 147)"""
    if sr_in == sr_out:
        return x
    from scipy.signal import resample_poly
    g = math.gcd(int(sr_in), int(sr_out))
    return resample_poly(x, int(sr_out) // g, int(sr_in) // g)


def read_f32(path):
    return np.fromfile(path, np.float32)


def write_f32(path, x):
    np.asarray(x, np.float32).tofile(path)


def write_wav(path, x, sr, bits=16, scale=1.0):
    """PCM (16 or 24 bit) or float (32) WAV; x mono [n] or [n, ch], scaled and clipped to +-1"""
    x = np.asarray(x, np.float64) * scale
    if x.ndim == 1:
        x = x[:, None]
    n, ch = x.shape
    if bits == 32:
        raw, tag = x.astype('<f4').tobytes(), 3
    else:
        q = np.round(np.clip(x, -1, 1) * (2 ** (bits - 1) - 1)).astype(np.int32)
        if bits == 16:
            raw = q.astype('<i2').tobytes()
        elif bits == 24:
            b = q.reshape(-1).astype('<i4').view(np.uint8).reshape(-1, 4)[:, :3]
            raw = b.tobytes()
        else:
            raise AudioError(f'{bits}-bit WAV is not written here')
        tag = 1
    nb = bits // 8
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(raw)) + b'WAVE')
        f.write(b'fmt ' + struct.pack('<IHHIIHH', 16, tag, ch, int(sr), int(sr) * nb * ch, nb * ch, bits))
        f.write(b'data' + struct.pack('<I', len(raw)) + raw)
