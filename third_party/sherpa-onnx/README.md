# sherpa-onnx

`c-api.h` is the public C API header of sherpa-onnx v2.0.1, copied unchanged from
`sherpa-onnx/c-api/c-api.h` at that tag. It is vendored because the Windows runtime archive does not
ship a header; the library itself comes from `sherpaOnnxRoot` in `config/paths.default.json`.

The header is Apache-2.0 (Copyright (c) 2023 Xiaomi Corporation). The runtime's notice is
`installer/THIRD_PARTY_NOTICES_VOICES.txt`.

To move to another version, replace `c-api.h` with the same tag's header and point `sherpaOnnxRoot`
at that tag's runtime. The struct layouts are the ABI, so the two must come from the same tag.
