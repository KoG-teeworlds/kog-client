#!/usr/bin/env python3
"""Test only: an OpenSSH signature of the form a security key makes, from a software key, so CI can exercise the verifier.

usage: sk_emulate.py KOGSIGN KEYFILE APPLICATION NAMESPACE FILE  ->  armored signature on stdout, public key hex on stderr
"""
import base64
import hashlib
import struct
import subprocess
import sys

kogsign, keyfile, application, namespace, path = sys.argv[1:6]
string = lambda data: struct.pack(">I", len(data)) + data
data = open(path, "rb").read()
signed = b"SSHSIG" + string(namespace.encode()) + string(b"") + string(b"sha512") + string(hashlib.sha512(data).digest())
flags, counter = 1, 7
message = hashlib.sha256(application.encode()).digest() + bytes([flags]) + struct.pack(">I", counter) + hashlib.sha256(signed).digest()
open(path + ".sk.msg", "wb").write(message)
signature = bytes.fromhex(subprocess.check_output([kogsign, "sign", keyfile, path + ".sk.msg"], text=True).split()[2])
public = bytes.fromhex(subprocess.check_output([kogsign, "pubkey", keyfile], text=True).strip())
key = "sk-ssh-ed25519@openssh.com"
blob = string(key.encode()) + string(public) + string(application.encode())
signature_blob = string(key.encode()) + string(signature) + bytes([flags]) + struct.pack(">I", counter)
outer = b"SSHSIG" + struct.pack(">I", 1) + string(blob) + string(namespace.encode()) + string(b"") + string(b"sha512") + string(signature_blob)
text = base64.b64encode(outer).decode()
print("-----BEGIN SSH SIGNATURE-----")
for i in range(0, len(text), 70):
    print(text[i:i + 70])
print("-----END SSH SIGNATURE-----")
print(public.hex(), file=sys.stderr)
