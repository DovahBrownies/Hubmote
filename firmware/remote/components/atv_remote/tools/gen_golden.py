r"""Generate src/atv_golden.h, the golden vectors for src/atv_selftest.c.

The on-device self-test checks the C encoder and parsers against bytes made
here with the official protobuf runtime:

- messages the remote sends, built exactly the way androidtvremote2's
  remote.py and pairing.py build them (and framed the way base.py frames them);
- messages a TV sends, for the parsers;
- a pairing-secret vector, hashed with pairing.py's own f-string +
  bytes.fromhex formatting of the RSA moduli and exponents.

Inputs are proto/remotemessage.proto and proto/polo.proto, vendored from
tronikos/androidtvremote2 (see proto/README.md for the pinned version). They
are compiled with grpc_tools.protoc into a temporary directory on each run;
no generated _pb2 files are kept.

Needs (in a throwaway venv, not installed globally):

    protobuf==7.36.2  grpcio-tools==1.84.0   (tested with Python 3.12.3)

The protobuf version is written into the header comment, so another version
changes that line even when the vectors stay the same.

Run from any directory; it rewrites the header in place. In PowerShell, from
the repository root:

    py -3.12 -m venv $env:TEMP\atvgen-venv
    & $env:TEMP\atvgen-venv\Scripts\python -m pip install protobuf==7.36.2 grpcio-tools==1.84.0
    & $env:TEMP\atvgen-venv\Scripts\python firmware\remote\components\atv_remote\tools\gen_golden.py
"""

from __future__ import annotations

import hashlib
import sys
import tempfile
from pathlib import Path

import google.protobuf
from google.protobuf.internal.encoder import _EncodeVarint  # what base.py uses for framing
from grpc_tools import protoc

COMPONENT = Path(__file__).resolve().parent.parent
PROTO_DIR = COMPONENT / "proto"
OUT_PATH = COMPONENT / "src" / "atv_golden.h"


def compile_protos(out_dir: str) -> None:
    args = [
        "grpc_tools.protoc",
        f"-I{PROTO_DIR}",
        f"--python_out={out_dir}",
        str(PROTO_DIR / "remotemessage.proto"),
        str(PROTO_DIR / "polo.proto"),
    ]
    if protoc.main(args) != 0:
        sys.exit("protoc failed")


# The _pb2 modules are compiled at import time so the message builders below
# can use their classes directly. The directory is removed when Python exits;
# the modules are already loaded by then.
_PB2_DIR = tempfile.TemporaryDirectory(prefix="atv_pb2_")
compile_protos(_PB2_DIR.name)
sys.path.insert(0, _PB2_DIR.name)

from polo_pb2 import Options, OuterMessage  # noqa: E402
from remotemessage_pb2 import (  # noqa: E402
    RemoteDirection,
    RemoteEditInfo,
    RemoteImeBatchEdit,
    RemoteImeObject,
    RemoteKeyCode,
    RemoteMessage,
)

# remote.py Feature flags
PING, KEY, IME, VOICE, POWER, VOLUME, APP_LINK = 1, 2, 4, 8, 32, 64, 512


# ------------------------------------------------------------ outgoing (remote.py)
def key_inject(key_code: int, direction: int) -> bytes:
    msg = RemoteMessage()
    msg.remote_key_inject.key_code = key_code
    msg.remote_key_inject.direction = direction
    return msg.SerializeToString()


def ping_response(val1: int) -> bytes:
    new_msg = RemoteMessage()
    new_msg.remote_ping_response.val1 = val1
    # val1=0 leaves the submessage empty; it must still be present.
    assert new_msg != RemoteMessage()
    return new_msg.SerializeToString()


def configure_reply(active: int) -> bytes:
    new_msg = RemoteMessage()
    new_msg.remote_configure.code1 = active
    new_msg.remote_configure.device_info.unknown1 = 1
    new_msg.remote_configure.device_info.unknown2 = "1"
    new_msg.remote_configure.device_info.package_name = "atvremote"
    new_msg.remote_configure.device_info.app_version = "1.0.0"
    return new_msg.SerializeToString()


def set_active_reply(active: int) -> bytes:
    new_msg = RemoteMessage()
    new_msg.remote_set_active.active = active
    return new_msg.SerializeToString()


def app_link(link: str) -> bytes:
    msg = RemoteMessage()
    msg.remote_app_link_launch_request.app_link = link
    return msg.SerializeToString()


def send_text(text: str, ime_counter: int, ime_field_counter: int) -> bytes:
    msg = RemoteMessage()
    param_value = len(text) - 1
    ime_object = RemoteImeObject(start=param_value, end=param_value, value=text)
    edit_info = RemoteEditInfo(insert=1, text_field_status=ime_object)
    batch_edit = RemoteImeBatchEdit(
        ime_counter=ime_counter,
        field_counter=ime_field_counter,
        edit_info=[edit_info],
    )
    msg.remote_ime_batch_edit.CopyFrom(batch_edit)
    return msg.SerializeToString()


# ----------------------------------------------------------- outgoing (pairing.py)
def _create_message() -> OuterMessage:
    msg = OuterMessage()
    msg.protocol_version = 2
    msg.status = OuterMessage.Status.STATUS_OK
    return msg


def pairing_request(client_name: str) -> bytes:
    msg = _create_message()
    msg.pairing_request.client_name = client_name
    msg.pairing_request.service_name = "atvremote"
    return msg.SerializeToString()


def options() -> bytes:
    new_msg = _create_message()
    new_msg.options.preferred_role = Options.RoleType.ROLE_TYPE_INPUT
    enc = new_msg.options.input_encodings.add()
    enc.type = Options.Encoding.ENCODING_TYPE_HEXADECIMAL
    enc.symbol_length = 6
    return new_msg.SerializeToString()


def configuration() -> bytes:
    new_msg = _create_message()
    new_msg.configuration.client_role = Options.RoleType.ROLE_TYPE_INPUT
    new_msg.configuration.encoding.type = Options.Encoding.ENCODING_TYPE_HEXADECIMAL
    new_msg.configuration.encoding.symbol_length = 6
    return new_msg.SerializeToString()


def secret(secret_bytes: bytes) -> bytes:
    msg = _create_message()
    msg.secret.secret = secret_bytes
    return msg.SerializeToString()


def frame(body: bytes) -> bytes:
    # base.py _send_message: varint length prefix, then the message.
    out = bytearray()
    _EncodeVarint(out.extend, len(body))
    return bytes(out) + body


# ------------------------------------------------------------- incoming (TV side)
def tv_configure() -> bytes:
    m = RemoteMessage()
    m.remote_configure.code1 = 639
    d = m.remote_configure.device_info
    d.model = "Google TV Streamer"
    d.vendor = "Google"
    d.unknown1 = 1
    d.unknown2 = "1"
    d.package_name = "com.google.android.tv.remote.service"
    d.app_version = "6.4.1"
    return m.SerializeToString()


def tv_set_active() -> bytes:
    m = RemoteMessage()
    m.remote_set_active.active = 622
    return m.SerializeToString()


def tv_ping(val1: int, val2: int) -> bytes:
    m = RemoteMessage()
    m.remote_ping_request.val1 = val1
    m.remote_ping_request.val2 = val2
    return m.SerializeToString()


def tv_ime_key_inject() -> bytes:
    m = RemoteMessage()
    m.remote_ime_key_inject.app_info.counter = 1
    m.remote_ime_key_inject.app_info.int13 = 1
    m.remote_ime_key_inject.app_info.app_package = "com.google.android.youtube.tv"
    m.remote_ime_key_inject.text_field_status.counter_field = 2
    m.remote_ime_key_inject.text_field_status.value = "abc"
    m.remote_ime_key_inject.text_field_status.start = 3
    m.remote_ime_key_inject.text_field_status.end = 3
    m.remote_ime_key_inject.text_field_status.label = "Search"
    return m.SerializeToString()


def tv_ime_batch_edit() -> bytes:
    m = RemoteMessage()
    m.remote_ime_batch_edit.ime_counter = 17
    m.remote_ime_batch_edit.field_counter = 42
    e = m.remote_ime_batch_edit.edit_info.add()
    e.insert = 1
    e.text_field_status.start = 1
    e.text_field_status.end = 1
    e.text_field_status.value = "ab"
    return m.SerializeToString()


def tv_ime_show() -> bytes:
    m = RemoteMessage()
    t = m.remote_ime_show_request.remote_text_field_status
    t.counter_field = 5
    t.value = "hello"
    t.start = 5
    t.end = 5
    t.int5 = 1
    t.label = "Search YouTube"
    return m.SerializeToString()


def tv_start(started: bool) -> bytes:
    m = RemoteMessage()
    m.remote_start.started = started
    # started=False leaves the submessage empty; it must still be present.
    assert m != RemoteMessage()
    return m.SerializeToString()


def tv_volume() -> bytes:
    m = RemoteMessage()
    v = m.remote_set_volume_level
    v.unknown1 = 2
    v.unknown2 = 3
    v.player_model = "Living Room TV"
    v.unknown4 = 4
    v.unknown5 = 5
    v.volume_max = 100
    v.volume_level = 27
    v.volume_muted = True
    return m.SerializeToString()


def tv_error() -> bytes:
    m = RemoteMessage()
    m.remote_error.value = True
    m.remote_error.message.remote_key_inject.key_code = RemoteKeyCode.KEYCODE_HOME
    return m.SerializeToString()


def tv_unknown_fields() -> bytes:
    # remote_start preceded by unknown fields of every wire type the parser
    # must skip: field 1000 varint, 1001 fixed64, 1002 bytes, 1003 fixed32.
    unknown = bytearray()
    _EncodeVarint(unknown.extend, (1000 << 3) | 0)
    _EncodeVarint(unknown.extend, 300)
    _EncodeVarint(unknown.extend, (1001 << 3) | 1)
    unknown += bytes(range(8))
    _EncodeVarint(unknown.extend, (1002 << 3) | 2)
    _EncodeVarint(unknown.extend, 3)
    unknown += b"xyz"
    _EncodeVarint(unknown.extend, (1003 << 3) | 5)
    unknown += bytes(4)
    data = bytes(unknown) + tv_start(True)
    m = RemoteMessage()
    m.ParseFromString(data)  # the official parser accepts it
    assert m.remote_start.started
    return data


def polo(status: int = OuterMessage.Status.STATUS_OK, **kw) -> bytes:
    m = OuterMessage()
    m.protocol_version = 2
    m.status = status
    if "server_name" in kw:
        m.pairing_request_ack.server_name = kw["server_name"]
    if kw.get("options"):
        m.options.preferred_role = Options.RoleType.ROLE_TYPE_INPUT
        e = m.options.input_encodings.add()
        e.type = Options.Encoding.ENCODING_TYPE_HEXADECIMAL
        e.symbol_length = 6
    if kw.get("configuration_ack"):
        m.configuration_ack.SetInParent()
    if "secret_ack" in kw:
        m.secret_ack.secret = kw["secret_ack"]
    return m.SerializeToString()


# ----------------------------------------------------------------- pairing secret
def big_modulus(seed: bytes, bits: int = 2048) -> int:
    # A fixed, odd number with the top bit set, from SHA-256 of the seed. It
    # doesn't need to be a real RSA modulus: the hash only uses its bytes.
    out = b""
    i = 0
    while len(out) < bits // 8:
        out += hashlib.sha256(seed + bytes([i])).digest()
        i += 1
    return int.from_bytes(out[: bits // 8], "big") | (1 << (bits - 1)) | 1


def pairing_secret(cn: int, ce: int, sn: int, se: int, pairing_code: str) -> bytes:
    # Verbatim from pairing.py async_finish_pairing.
    h = hashlib.sha256()
    h.update(bytes.fromhex(f"{cn:X}"))
    h.update(bytes.fromhex(f"0{ce:X}"))
    h.update(bytes.fromhex(f"{sn:X}"))
    h.update(bytes.fromhex(f"0{se:X}"))
    h.update(bytes.fromhex(pairing_code[2:]))
    return h.digest()


def minimal(x: int) -> bytes:
    # Shortest big-endian bytes of x, as mbedtls_mpi_write_binary would give.
    return x.to_bytes((x.bit_length() + 7) // 8, "big")


# ---------------------------------------------------------------------- C output
def c_bytes(name: str, data: bytes) -> str:
    lines = [f"static const uint8_t {name}[{max(len(data), 1)}] = {{"]
    for i in range(0, len(data), 16):
        lines.append("    " + " ".join(f"0x{b:02x}," for b in data[i : i + 16]))
    if not data:
        lines.append("    0x00,")
    lines.append("};")
    lines.append(f"#define {name.upper()}_LEN {len(data)}")
    return "\n".join(lines)


def c_str(s: str) -> str:
    # A C string literal of the UTF-8 bytes. Each \x escape is closed with ""
    # so the next character can't be read as more hex digits.
    out = '"'
    for ch in s.encode("utf-8"):
        if 0x20 <= ch < 0x7F and ch not in (0x22, 0x5C):
            out += chr(ch)
        else:
            out += f'\\x{ch:02x}""'
    return out + '"'


def main() -> None:
    long_text = "".join(chr(ord("a") + (i % 26)) for i in range(200))
    utf8_text = "Caf\u00e9 \u00fc\u20ac"  # 7 code points, 11 UTF-8 bytes

    vectors: list[tuple[str, bytes, str]] = [
        # name, bytes, description
        ("g_key_dpad_up_short", key_inject(RemoteKeyCode.KEYCODE_DPAD_UP, RemoteDirection.SHORT),
         "remote_key_inject{KEYCODE_DPAD_UP, SHORT}"),
        ("g_key_hdmi1_start_long", key_inject(RemoteKeyCode.KEYCODE_TV_INPUT_HDMI_1, RemoteDirection.START_LONG),
         "remote_key_inject{KEYCODE_TV_INPUT_HDMI_1 (243), START_LONG}"),
        ("g_key_home_end_long", key_inject(RemoteKeyCode.KEYCODE_HOME, RemoteDirection.END_LONG),
         "remote_key_inject{KEYCODE_HOME, END_LONG}"),
        ("g_key_unknown_unknown", key_inject(0, 0), "remote_key_inject{0, 0} (all defaults)"),
        ("g_ping_resp_0", ping_response(0), "remote_ping_response{val1=0}"),
        ("g_ping_resp_5", ping_response(5), "remote_ping_response{val1=5}"),
        ("g_ping_resp_neg1", ping_response(-1), "remote_ping_response{val1=-1}"),
        ("g_ping_resp_big", ping_response(123456789), "remote_ping_response{val1=123456789}"),
        ("g_configure_615", configure_reply(PING | KEY | IME | POWER | VOLUME | APP_LINK),
         "remote_configure reply, features with IME (615)"),
        ("g_configure_611", configure_reply(PING | KEY | POWER | VOLUME | APP_LINK),
         "remote_configure reply, features without IME (611)"),
        ("g_set_active_615", set_active_reply(615), "remote_set_active{active=615}"),
        ("g_app_link_netflix", app_link("https://www.netflix.com/title/80057281"),
         "remote_app_link_launch_request{https://www.netflix.com/title/80057281}"),
        ("g_app_link_market", app_link("market://launch?id=com.google.android.youtube.tv"),
         "remote_app_link_launch_request{market://launch?id=com.google.android.youtube.tv}"),
        ("g_text_hello", send_text("Hello", 3, 7), "send_text('Hello') ime_counter=3 field_counter=7"),
        ("g_text_h", send_text("h", 0, 0), "send_text('h') counters 0 (start=end=0 omitted)"),
        ("g_text_utf8", send_text(utf8_text, 1, 2), "send_text('Caf\\u00e9 \\u00fc\\u20ac') counters 1/2"),
        ("g_text_long", send_text(long_text, 300, 129), "send_text(200 chars) counters 300/129"),
        ("g_frame_text_long", frame(send_text(long_text, 300, 129)), "varint-framed g_text_long (base.py)"),
        ("g_frame_key", frame(key_inject(RemoteKeyCode.KEYCODE_DPAD_UP, RemoteDirection.SHORT)),
         "varint-framed g_key_dpad_up_short"),
        ("g_polo_pairing_request", pairing_request("Hubmote"),
         "OuterMessage{2, 200, pairing_request{atvremote, Hubmote}}"),
        ("g_polo_options", options(), "OuterMessage{2, 200, options{[HEX,6], INPUT}}"),
        ("g_polo_configuration", configuration(), "OuterMessage{2, 200, configuration{{HEX,6}, INPUT}}"),
        ("g_polo_secret", secret(bytes(range(32))), "OuterMessage{2, 200, secret{00..1f}}"),
        # incoming
        ("g_in_configure", tv_configure(), "TV remote_configure"),
        ("g_in_set_active", tv_set_active(), "TV remote_set_active{622}"),
        ("g_in_ping", tv_ping(12, 34), "TV remote_ping_request{12, 34}"),
        ("g_in_ping_neg", tv_ping(-7, 0), "TV remote_ping_request{-7}"),
        ("g_in_ime_key_inject", tv_ime_key_inject(), "TV remote_ime_key_inject"),
        ("g_in_ime_batch_edit", tv_ime_batch_edit(), "TV remote_ime_batch_edit{17, 42}"),
        ("g_in_ime_show", tv_ime_show(), "TV remote_ime_show_request"),
        ("g_in_start_true", tv_start(True), "TV remote_start{true}"),
        ("g_in_start_false", tv_start(False), "TV remote_start{false}"),
        ("g_in_volume", tv_volume(), "TV remote_set_volume_level"),
        ("g_in_error", tv_error(), "TV remote_error{true, remote_key_inject}"),
        ("g_in_unknown_fields", tv_unknown_fields(), "unknown fields + remote_start{true}"),
        ("g_in_polo_req_ack", polo(server_name="Google TV Streamer"), "TV pairing_request_ack"),
        ("g_in_polo_options", polo(options=True), "TV options"),
        ("g_in_polo_config_ack", polo(configuration_ack=True), "TV configuration_ack"),
        ("g_in_polo_secret_ack", polo(secret_ack=bytes([0xAA] * 32)), "TV secret_ack"),
        ("g_in_polo_bad_secret", polo(OuterMessage.Status.STATUS_BAD_SECRET), "TV status 402"),
    ]

    cn = big_modulus(b"atv-client")
    sn = big_modulus(b"atv-server")
    e = 65537
    # The first byte of the code is the first byte of the hash, and only the
    # last 4 digits enter the hash: hash once, then put its first byte in front.
    tail = "3C4D"
    h0 = pairing_secret(cn, e, sn, e, "00" + tail)
    code = f"{h0[0]:02X}{tail}"
    h = pairing_secret(cn, e, sn, e, code)
    assert h == h0 and h[0] == int(code[0:2], 16)
    # The C code hashes minimal big-endian bytes; check that equals pairing.py's
    # f-string formatting for these values.
    assert bytes.fromhex(f"{cn:X}") == minimal(cn)
    assert bytes.fromhex(f"0{e:X}") == minimal(e) == b"\x01\x00\x01"

    out = [
        "#pragma once",
        "",
        "/*",
        " * GENERATED by tools/gen_golden.py (python protobuf "
        + google.protobuf.__version__
        + ", protos compiled with grpc_tools.protoc",
        " * from remotemessage.proto / polo.proto). Do not edit by hand.",
        " * Outgoing messages are built exactly as remote.py / pairing.py build them;",
        " * the secret vector uses pairing.py's f-string + bytes.fromhex formatting.",
        " */",
        "",
        "#include <stdint.h>",
        "",
    ]
    for name, data, desc in vectors:
        out.append(f"/* {desc} */")
        out.append(c_bytes(name, data))
        out.append("")
    out.append("/* text inputs used for g_text_* */")
    out.append(f"#define G_TEXT_UTF8 {c_str(utf8_text)}")
    out.append("")
    out.append("/* Pairing secret vector: deterministic 2048-bit moduli, e = 65537. */")
    out.append(c_bytes("g_secret_client_n", minimal(cn)))
    out.append(c_bytes("g_secret_server_n", minimal(sn)))
    out.append(f"#define G_SECRET_E {e}")
    out.append(f'#define G_SECRET_CODE "{code}"')
    out.append(c_bytes("g_secret_hash", h))
    out.append("")
    with open(OUT_PATH, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(out))
    print(f"{len(vectors)} message vectors + secret vector -> {OUT_PATH} (code {code})")


if __name__ == "__main__":
    main()
