#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the built server's X11 selections with an Apple-wire-format client.

Only synthetic text and a temporary password are used. Requires Xvfb, xclip,
Python 3.6+ and openssl. The actual Mac client is tested separately.
"""
import contextlib
import ctypes
import os
import pathlib
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib

TEXT = 'clipboard café 日本語\nsecond line\n'.encode('utf-8')


def sized(data):
    return struct.pack('>I', len(data)) + data


def clipboard(promise=False, text=TEXT):
    aliases = [(b'com.apple.ostype', b'utf8'),
               (b'com.apple.nspboard-type', b'NSStringPboardType'),
               (b'public.mime-type', b'text/plain;charset=utf-8')]
    archive = struct.pack('>I', 1) + sized(b'public.utf8-plain-text')
    archive += struct.pack('>II', 0, len(aliases))
    for key, value in aliases:
        archive += sized(key) + sized(value)
    archive += sized(b'' if promise else text)
    compressor = zlib.compressobj()
    packed = compressor.compress(archive) + compressor.flush(zlib.Z_SYNC_FLUSH)
    return struct.pack('>BxB5xII', 31, promise, len(archive), len(packed)) + packed


class Client:
    def __init__(self, port):
        self.sock = socket.create_connection(('127.0.0.1', port), timeout=15)
        assert self.read(12) == b'RFB 003.889\n'
        self.sock.sendall(b'RFB 003.889\n')
        assert self.read(2) == b'\x01\x02'
        challenge = self.read(16)
        key = bytes(int('{:08b}'.format(c)[::-1], 2) for c in b'probe\0\0\0')
        args = ['openssl', 'enc', '-des-ecb', '-K', key.hex(), '-nopad', '-nosalt']
        if subprocess.check_output(['openssl', 'version']).startswith(b'OpenSSL 3'):
            args += ['-provider', 'legacy']
        response = subprocess.check_output(args, input=challenge)
        self.sock.sendall(response)
        assert self.read(4) == b'\0' * 4, 'VNC password authentication failed'
        self.sock.sendall(b'\xc1')
        header = self.read(24)
        name = self.read(struct.unpack('>I', header[20:24])[0])
        assert name[:6] == b'\0' * 6 and name[9] & 1, 'Apple capability prefix missing'
        self.sock.sendall(b'\x02\0\0\0')  # standard SetEncodings, zero encodings
        self.sock.sendall(bytes([21, 0, 0, 1, 0, 0, 0, 0]))

    def read(self, size):
        data = b''
        while len(data) < size:
            part = self.sock.recv(size - len(data))
            if not part:
                raise RuntimeError('Server disconnected')
            data += part
        return data

    def message(self):
        kind = self.read(1)[0]
        if kind == 20:
            pad, length, flags, command = struct.unpack('>BHHH', self.read(7))
            assert (pad, length, flags) == (0, 4, 1)
            return kind, command
        if kind == 31:
            header = self.read(15)
            expanded, compressed = struct.unpack('>II', header[7:])
            assert max(expanded, compressed) < 1024 * 1024
            raw = zlib.decompressobj().decompress(self.read(compressed), expanded + 1)
            assert len(raw) == expanded
            return kind, raw
        raise AssertionError('Unexpected server message {}'.format(kind))

    def wait(self, kind, command=None):
        for _ in range(20):
            got, value = self.message()
            if got == kind and (command is None or command == value):
                return value
        raise AssertionError('Expected clipboard message was not received')


@contextlib.contextmanager
def process(args, **kwargs):
    child = subprocess.Popen(args, **kwargs)
    try:
        yield child
    finally:
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()


def main():
    build = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='tigervnc-apple-test-') as directory:
        root = pathlib.Path(directory)
        password = root / 'passwd'
        password.write_bytes(subprocess.check_output(
            [str(build / 'unix/vncpasswd/vncpasswd'), '-f'], input=b'probe\n'))
        password.chmod(0o600)
        # Xvfb chooses a free display and returns its number over this pipe.
        read_fd, write_fd = os.pipe()
        with process(['Xvfb', '-displayfd', str(write_fd), '-screen', '0', '640x480x24',
                      '-nolisten', 'tcp'], pass_fds=(write_fd,),
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL):
            os.close(write_fd)
            with os.fdopen(read_fd, 'rb') as display_pipe:
                display = display_pipe.readline().decode().strip()
            assert display, 'Xvfb did not start'
            env = dict(os.environ, DISPLAY=':' + display)
            xlib = ctypes.CDLL('libX11.so.6')
            xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
            xlib.XOpenDisplay.restype = ctypes.c_void_p
            xlib.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
            xlib.XInternAtom.restype = ctypes.c_ulong
            xlib.XGetSelectionOwner.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
            xlib.XGetSelectionOwner.restype = ctypes.c_ulong
            xdisplay = xlib.XOpenDisplay(env['DISPLAY'].encode())
            assert xdisplay, 'Cannot query X11 selection ownership'
            atom = xlib.XInternAtom(xdisplay, b'CLIPBOARD', 0)
            # This isolated test uses only a loopback VNC listener.
            with socket.socket() as port_picker:
                port_picker.bind(('127.0.0.1', 0))
                port = port_picker.getsockname()[1]
            with (root / 'server.log').open('wb') as log:
                args = [str(build / 'unix/x0vncserver/x0vncserver'),
                        '-display', env['DISPLAY'], '-rfbport', str(port), '-localhost',
                        '-PasswordFile', str(password), '-SecurityTypes', 'VncAuth',
                        '-AppleClipboard']
                with process(args, env=env, stdout=log, stderr=log) as server:
                    for _ in range(100):
                        if server.poll() is not None:
                            raise RuntimeError((root / 'server.log').read_text())
                        try:
                            client = Client(port)
                            break
                        except ConnectionRefusedError:
                            time.sleep(0.1)
                    else:
                        raise RuntimeError('Server did not start')
                    try:
                        # Linux -> Mac wire protocol: clipboard notification, promise,
                        # then a demand-driven request for UTF-8 data.
                        with process(['xclip', '-selection', 'clipboard', '-in', '-quiet'],
                                     env=env, stdin=subprocess.PIPE,
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) as owner:
                            owner.stdin.write(TEXT)
                            owner.stdin.close()
                            client.wait(20, 2)
                            client.sock.sendall(bytes([11, 1, 0, 0, 0, 0, 0, 0]))
                            assert b'public.utf8-plain-text' in client.wait(31)
                            client.sock.sendall(bytes([11, 0, 0, 0, 0, 0, 0, 0]))
                            assert TEXT in client.wait(31)
                            previous_owner = xlib.XGetSelectionOwner(xdisplay, atom)
                        # x0vncserver fetches advertised text immediately so it
                        # can own the X selection; it then serves local apps.
                        client.sock.sendall(clipboard(promise=True))
                        client.wait(20, 3)
                        client.sock.sendall(clipboard())
                        for _ in range(100):
                            current_owner = xlib.XGetSelectionOwner(xdisplay, atom)
                            if current_owner and current_owner != previous_owner:
                                break
                            time.sleep(0.05)
                        else:
                            raise AssertionError('TigerVNC did not claim the X11 clipboard')
                        with process(['xclip', '-selection', 'clipboard', '-out'],
                                     env=env, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE) as reader:
                            result, errors = reader.communicate(timeout=15)
                            assert reader.returncode == 0, errors.decode()
                            assert result == TEXT, 'X11 clipboard contents did not match'
                        print('PASS: X11 clipboard in both directions (UTF-8/newlines)')
                    finally:
                        client.sock.close()
                        xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]
                        xlib.XCloseDisplay(xdisplay)


if __name__ == '__main__':
    main()
