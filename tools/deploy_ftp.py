"""Inspect or install the two YouTubeDSi files over the device's FTP server."""
import argparse
import ftplib
import hashlib
import io
import json
import os
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("host")
parser.add_argument("--port", type=int, default=5000)
parser.add_argument("--user", default="anonymous")
parser.add_argument("--root", default="/")
parser.add_argument("--deploy", action="store_true")
args = parser.parse_args()
ftp = ftplib.FTP(timeout=15)
ftp.connect(args.host, args.port)
ftp.login(args.user, os.environ.get("DSI_FTP_PASSWORD", ""))
ftp.cwd(args.root)
names = ftp.nlst()
print("FTP root:", ftp.pwd(), "entries:", names[:40], flush=True)
if not args.deploy:
    ftp.quit()
    raise SystemExit(0)
if not any(name.rstrip("/").split("/")[-1] in ("_nds", "BOOT.NDS", "roms") for name in names):
    raise SystemExit("SD root not recognized. Inspect the listing and choose --root first.")

backup = ROOT / "device-backups" / datetime.now().strftime("%Y%m%d-%H%M%S")
report = {"host": args.host, "port": args.port, "root": ftp.pwd(), "files": []}
for relative in ("roms/tools/YouTubeDSi.nds", "youtube-dsi.ini"):
    parent, _, filename = relative.rpartition("/")
    ftp.cwd(args.root)
    for part in parent.split("/") if parent else []:
        try:
            ftp.cwd(part)
        except ftplib.error_perm:
            ftp.mkd(part)
            ftp.cwd(part)
    local = (ROOT / "sd-root" / relative).read_bytes()
    existing = [name.rstrip("/").split("/")[-1] for name in ftp.nlst()]
    if filename in existing:
        old = io.BytesIO()
        ftp.retrbinary("RETR " + filename, old.write)
        saved = backup / relative
        saved.parent.mkdir(parents=True, exist_ok=True)
        saved.write_bytes(old.getvalue())
    print("Uploading", relative, len(local), "bytes", flush=True)
    ftp.storbinary("STOR " + filename, io.BytesIO(local))
    checked = io.BytesIO()
    ftp.retrbinary("RETR " + filename, checked.write)
    if checked.getvalue() != local:
        raise RuntimeError("Device read-back differs: " + relative)
    report["files"].append({"path": relative, "bytes": len(local),
                            "sha256": hashlib.sha256(local).hexdigest(), "read_back_match": True})
ftp.quit()
(ROOT / "device-deployment.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps(report, indent=2))
