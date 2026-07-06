# execute-tts-packager.py
# Post-build script for the [env:native] PlatformIO environment.
# Builds the TTS packager, runs it if models changed, then flashes the LittleFS partition
# to the connected ESP32 using PlatformIO's built-in targets.

import hashlib
import shutil
import subprocess
import sys
import os
from pathlib import Path
from SCons.Script import Import

Import("env")
env.Replace(UPLOADCMD="echo [eccles-tts] native env: skipping upload")

def directory_hash(folder):
    h = hashlib.sha256()
    for path in sorted(folder.rglob("*")):
        if path.is_file():
            h.update(str(path.relative_to(folder)).encode())
            with open(path, "rb") as f:
                while chunk := f.read(8192):
                    h.update(chunk)
    return h.hexdigest()

def after_build(source, target, env):
    project_dir = Path(env.subst("$PROJECT_DIR"))
    models_dir  = project_dir / "eccles" / "resources" / "models"
    hash_file   = project_dir / ".models_hash"

    if not models_dir.exists():
        print(f"[eccles-tts] ERROR: models directory not found: {models_dir}")
        return

    current_hash  = directory_hash(models_dir)
    previous_hash = hash_file.read_text().strip() if hash_file.exists() else ""

    if current_hash == previous_hash:
        print("[eccles-tts] models unchanged, skipping packager")
        return

    # Extract target as string safely
    built_exe = Path(str(target[0]))
    bin_dir   = project_dir / "eccles" / "tools" / "bin"
    packager  = bin_dir / "eccles-tts-packager.exe"

    bin_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(built_exe, packager)
    print(f"[eccles-tts] installed packager: {packager}")

    print("[eccles-tts] running packager...")
    # Force explicit string parameters to avoid parameter truncation inside the child process
    result = subprocess.run([str(packager), "-platformIO"], cwd=str(bin_dir))
    print(f"[eccles-tts] packager returned {result.returncode}")

    if result.returncode != 0:
        print("[eccles-tts] packager failed, LittleFS flash cancelled")
        return

    # Explicit local PlatformIO runner path execution block
    pio_path = r"C:\Eccles\penv\Scripts\platformio.exe"
    if not os.path.exists(pio_path):
        pio_path = "platformio"

    print("[eccles-tts] Flashing LittleFS partition automatically via PlatformIO uploadfs...")
    
    # Run the compiler task explicitly targeting the filesystem generation engine
    flash_result = subprocess.run(
        [pio_path, "run", "-e", "esp32", "-t", "uploadfs"],
        cwd=str(project_dir),
        shell=True
    )

    if flash_result.returncode == 0:
        hash_file.write_text(current_hash)
        print("[eccles-tts] SUCCESS: LittleFS data partition successfully flashed!")
    else:
        print(f"[eccles-tts] ERROR: LittleFS flash failed with code {flash_result.returncode}")

env.AddPostAction("$PROGPATH", after_build)
