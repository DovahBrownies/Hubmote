# Vendored protos

`remotemessage.proto` and `polo.proto` are unchanged copies of
`src/androidtvremote2/` from
[tronikos/androidtvremote2](https://github.com/tronikos/androidtvremote2)
release v0.3.2 (commit `3f30c275615fc5eb11b4a2da4fe33a02cbb4fd66`), which is
licensed under Apache-2.0; `LICENSE` is that project's license file. Upstream
took them from [louis49/androidtv-remote](https://github.com/louis49/androidtv-remote)
(`remotemessage.proto`, and the change in `polo.proto`'s `OuterMessage`),
which is under the MIT License (`LICENSE-androidtv-remote`, its license
file), and from AOSP's google-tv-pairing-protocol (Apache-2.0, Copyright
2009 Google Inc.), as their first lines say. They are the inputs of
`../tools/gen_keycodes.py` and `../tools/gen_golden.py`, not part of the
firmware build. To update them, copy both files from a newer tag, record the
tag and commit here, and run both generators.

Keep the header comments of both files and both license files: Apache-2.0
(section 4) requires keeping the attribution notices of the original files,
and the MIT License requires its notice to go with copies.
