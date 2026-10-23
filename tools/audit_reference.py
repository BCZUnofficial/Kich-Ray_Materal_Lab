#!/usr/bin/env python3
"""Run the portable reference auditor and attach file hashes and pinned evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--binary", type=Path, required=True)
parser.add_argument("--bump", type=Path, required=True)
parser.add_argument("--bumpx", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

before = [digest(args.bump), digest(args.bumpx)]
result = subprocess.run([str(args.binary.resolve()), str(args.bump.resolve()), str(args.bumpx.resolve())], check=True, text=True, capture_output=True)
if before != [digest(args.bump), digest(args.bumpx)]:
    raise RuntimeError("Input reference bytes changed during audit")
metrics = json.loads(result.stdout)
commit = "d7b23596a70374d8a7ffda0e98852f93ce985182"
metrics["reference_files"] = [{"name": p.name, "size_bytes": p.stat().st_size, "sha256": sha} for p, sha in zip((args.bump, args.bumpx), before)]
metrics["primary_sources"] = {
    "shader": f"https://github.com/OpenXRay/xray/blob/{commit}/trunk/resources/shaders/r2/sload.h",
    "sdk_converter": f"https://github.com/OpenXRay/xray/blob/{commit}/trunk/xrDXT/NormalMapGen.cpp",
}
metrics["interpretation"] = {
    "normal_basis": "Layer editing stores the decoded normal before the SoC shader's final z *= 0.5. Rendered angular errors include that scale once.",
    "gloss": "The shader uses bump.r squared. UI layer gloss is linear and packing takes its square root.",
    "height": "Parallax uses bumpX alpha. RGB stores normal residuals, not RGB height.",
    "residual": "SDK emits 128 + 2*error/3. Material Lab uses full residual because actual double-BC3 encode/decode gives lower angular error on this fixture with its encoder. The format matches; output is not bit-identical to SDK conversion.",
    "checks": "No-op preview and gloss-only normal channels must change exactly zero bytes. Unchanged raw DDS export must be byte exact. Angular/RMSE metrics describe this fixture; they are not guarantees for arbitrary textures.",
    "limitations": "Edited DDS export is lossy BC3. No-op byte preservation applies to raw original DDS export; edited files and their derived mipmaps are recompressed.",
}
root = Path(__file__).resolve().parents[1]
metrics["source_file_sha256_at_audit_time"] = {name: digest(root / name) for name in ("src/Core.cpp", "src/DDS.cpp", "tools/MaterialReferenceAudit.cpp")}
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(metrics, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(args.output)
