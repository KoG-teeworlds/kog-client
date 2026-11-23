#!/usr/bin/env python3
import base64
import os
import re
import struct
import sys

HEADER = os.environ.get("KOG_PUBKEY_HEADER", "src/engine/client/kog_update_pubkey.h")


def array(text, name):
    match = re.search(r"static const char \*const %s\[\] = \{([^}]*)\};" % name, text)
    if not match:
        sys.exit("%s is not in the header" % name)
    return [key for key in re.findall(r'"([0-9a-f]*)"', match.group(1)) if key]


def set_array(text, name, keys):
    body = ", ".join('"%s"' % key for key in keys)
    text, count = re.subn(r"(static const char \*const %s\[\] = )\{[^}]*\};" % name, lambda m: m.group(1) + "{" + body + "};", text)
    if count != 1:
        sys.exit("%s is not in the header once" % name)
    return text


def application(text):
    match = re.search(r'#define KOG_OWNER_KEY_APPLICATION "([^"]*)"', text)
    if not match:
        sys.exit("KOG_OWNER_KEY_APPLICATION is not in the header")
    return match.group(1)


def string(data):
    return struct.pack(">I", len(data)) + data


def allowed_signers(text, namespace="kog-client-release"):
    lines = []
    for key in array(text, "KOG_STABLE_OWNER_KEYS"):
        blob = string(b"sk-ssh-ed25519@openssh.com") + string(bytes.fromhex(key)) + string(application(text).encode())
        lines.append('owner namespaces="%s" sk-ssh-ed25519@openssh.com %s' % (namespace, base64.b64encode(blob).decode()))
    return "\n".join(lines)


def sk_public_hex(line):
    parts = line.split()
    if len(parts) < 2 or parts[0] != "sk-ssh-ed25519@openssh.com":
        sys.exit("not a security key public key (sk-ssh-ed25519@openssh.com)")
    blob = base64.b64decode(parts[1], validate=True)
    fields, offset = [], 0
    while offset < len(blob):
        length = struct.unpack(">I", blob[offset:offset + 4])[0]
        fields.append(blob[offset + 4:offset + 4 + length])
        offset += 4 + length
    if len(fields) != 3 or len(fields[1]) != 32:
        sys.exit("malformed security key public key")
    if fields[2].decode() != application_in_header():
        sys.exit("the key application %r is not the one the client expects" % fields[2].decode())
    return fields[1].hex()


def application_in_header():
    return application(open(HEADER).read())


def set_owner(text, owners, backup):
    text = set_array(text, "KOG_STABLE_OWNER_KEYS", owners)
    text = set_array(text, "KOG_STABLE_BACKUP_CI_KEYS", backup)
    ready = bool(owners) and len(array(text, "KOG_STABLE_CI_KEYS")) == 2
    return re.sub(r"#define KOG_UPDATE_KEYS_PLACEHOLDER_STABLE [01]", "#define KOG_UPDATE_KEYS_PLACEHOLDER_STABLE %d" % (0 if ready else 1), text)


def main(argv):
    if len(argv) < 2:
        sys.exit("usage: keys_from_header.py ci CHANNEL | backup | owners | allowed-signers | set-owner OWNER_HEX... --backup HEX...")
    path = HEADER
    text = open(path).read()
    command = argv[1]
    if command == "ci":
        print("\n".join(array(text, "KOG_%s_CI_KEYS" % argv[2].upper())))
    elif command == "backup":
        print("\n".join(array(text, "KOG_STABLE_BACKUP_CI_KEYS")))
    elif command == "owners":
        print("\n".join(array(text, "KOG_STABLE_OWNER_KEYS")))
    elif command == "allowed-signers":
        print(allowed_signers(text))
    elif command == "pub-hex":
        print(sk_public_hex(open(argv[2]).read()))
    elif command == "set-owner":
        rest = argv[2:]
        split = rest.index("--backup")
        owners, backup = rest[:split], rest[split + 1:]
        for key in owners + backup:
            if not re.fullmatch(r"[0-9a-f]{64}", key):
                sys.exit("not a 32 byte hex key: " + key[:16])
        open(path, "w").write(set_owner(text, owners, backup))
    else:
        sys.exit("unknown command")


if __name__ == "__main__":
    main(sys.argv)
