#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Test a real Xvnc with an Apple protocol peer and the actual TigerVNC viewer.

Requires a Linux Xorg-based Xvnc build, Xvfb, xclip, xdotool and OpenSSL.
Uses isolated synthetic displays, loopback listeners and a temporary password.
"""
import contextlib
import os
import pathlib
import select
import socket
import struct
import subprocess
import sys
import tempfile
import time

from x11_clipboard_smoke import Client, clipboard, process


@contextlib.contextmanager
def display(args, log):
    read_fd, write_fd = os.pipe()
    try:
        with process(args + ['-displayfd', str(write_fd), '-nolisten', 'tcp'],
                     pass_fds=(write_fd,), stdout=log, stderr=log) as child:
            os.close(write_fd)
            write_fd = -1
            if not select.select([read_fd], [], [], 20)[0]:
                raise RuntimeError('X server did not report a display')
            number = os.read(read_fd, 128).strip().decode()
            assert number.isdigit(), 'X server startup failed'
            yield dict(os.environ, DISPLAY=':' + number), child
    finally:
        os.close(read_fd)
        if write_fd != -1:
            os.close(write_fd)


@contextlib.contextmanager
def own_clipboard(env, text):
    with process(['xclip', '-selection', 'clipboard', '-in', '-quiet'], env=env,
                 stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                 stderr=subprocess.DEVNULL) as child:
        child.stdin.write(text)
        child.stdin.close()
        yield


def read_clipboard(env):
    # Use process() so a timed-out clipboard owner cannot leave a child behind.
    with process(['xclip', '-selection', 'clipboard', '-out'], env=env,
                 stdout=subprocess.PIPE, stderr=subprocess.PIPE) as child:
        result, error = child.communicate(timeout=5)
        if child.returncode:
            raise RuntimeError(error.decode())
        return result


def wait_clipboard(env, expected):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        try:
            if read_clipboard(env) == expected:
                return
        except (RuntimeError, subprocess.TimeoutExpired):
            pass
        time.sleep(0.1)
    raise AssertionError('Clipboard did not reach the expected content')


def apple_roundtrip(port, server_env):
    # Reconnect between rounds, and cover empty text separately from no flavor.
    for text in ['Unicode café 日本語\nsecond line\n'.encode(), b'']:
        client = Client(port)
        try:
            with own_clipboard(server_env, text):
                client.wait(20, 2)
                client.sock.sendall(bytes([11, 1, 0, 0, 0, 0, 0, 0]))
                assert b'public.utf8-plain-text' in client.wait(31)
                client.sock.sendall(bytes([11, 0, 0, 0, 0, 0, 0, 0]))
                archive = client.wait(31)
                # Length-prefixed text is the final archive field.
                assert archive.endswith(struct.pack('>I', len(text)) + text)
            client.sock.sendall(clipboard(promise=True, text=text))
            # Xvnc fetches lazily when an X client requests the selection.
            with process(['xclip', '-selection', 'clipboard', '-out'], env=server_env,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE) as reader:
                client.wait(20, 3)
                client.sock.sendall(clipboard(text=text))
                got, errors = reader.communicate(timeout=10)
                assert reader.returncode == 0, errors.decode()
                assert got == text
        finally:
            client.sock.close()
    print('PASS: Xvnc Apple protocol, both directions, Unicode, empty text, reconnect')


def viewer_roundtrip(build, port, server_env, password, log):
    with display(['Xvfb', '-screen', '0', '800x600x24'], log) as (viewer_env, _):
        args = [str(build / 'vncviewer/vncviewer'), '-Shared',
                '-PasswordFile', str(password), '-SecurityTypes', 'VncAuth',
                '-SendPrimary=0', '-SetPrimary=0', '-AlertOnFatalError=0',
                '127.0.0.1::' + str(port)]
        with process(args, env=viewer_env, stdout=log, stderr=log) as viewer:
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                assert viewer.poll() is None, 'TigerVNC viewer exited'
                windows = subprocess.run(
                    ['xdotool', 'search', '--onlyvisible', '--class', 'vncviewer'],
                    env=viewer_env, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
                if windows.returncode == 0:
                    window = windows.stdout.splitlines()[-1].decode()
                    subprocess.run(['xdotool', 'windowfocus', window],
                                   env=viewer_env, check=True)
                    break
                time.sleep(0.1)
            else:
                raise AssertionError('TigerVNC viewer window did not appear')
            text = 'TigerVNC to Xvnc café 日本語\n'.encode()
            with own_clipboard(viewer_env, text):
                wait_clipboard(server_env, text)
            text = 'Xvnc to TigerVNC café 日本語\n'.encode()
            with own_clipboard(server_env, text):
                wait_clipboard(viewer_env, text)
            # An Apple client connects alongside the viewer, not exclusively.
            apple_roundtrip(port, server_env)
            assert viewer.poll() is None
    print('PASS: actual TigerVNC Viewer, both clipboard directions, simultaneous Apple client')


def main():
    build = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='tigervnc-xvnc-interop-') as directory:
        root = pathlib.Path(directory)
        password = root / 'passwd'
        password.write_bytes(subprocess.check_output(
            [str(build / 'unix/vncpasswd/vncpasswd'), '-f'], input=b'probe\n'))
        password.chmod(0o600)
        with socket.socket() as picker:
            picker.bind(('127.0.0.1', 0))
            port = picker.getsockname()[1]
        log_path = root / 'interop.log'
        try:
            with log_path.open('wb') as log:
                args = [str(build / 'unix/xserver/hw/vnc/Xvnc'), '-geometry', '640x480',
                        '-depth', '24', '-rfbport', str(port), '-localhost',
                        '-PasswordFile', str(password), '-SecurityTypes', 'VncAuth',
                        '-AppleClipboard', '-AlwaysShared']
                with display(args, log) as (server_env, _):
                    apple_roundtrip(port, server_env)
                    viewer_roundtrip(build, port, server_env, password, log)
        except Exception:
            print(log_path.read_text(errors='replace'), file=sys.stderr)
            raise


if __name__ == '__main__':
    main()
