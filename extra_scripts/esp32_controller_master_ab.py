#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): PlatformIO SCons injects Import/Return.
"""Link the pinned 2026-09-07 ESP32 controller master blob for an isolated A/B."""

import sys
from pathlib import Path

Import("env")

if env.IsIntegrationDump():
    Return()

board = env.BoardConfig()
if str(board.get("build.mcu", "")).lower() != "esp32":
    Return()

project_dir = Path(env.subst("$PROJECT_DIR"))
sys.path.insert(0, str(project_dir / "extra_scripts"))
from esp32_controller_master_ab_support import (  # noqa: E402
    CONTROLLER_BLOB_SHA1,
    CONTROLLER_COMMIT,
    controller_archive_member_paths,
    ensure_controller_archive,
    is_controller_library_entry,
)

try:
    controller_archive = ensure_controller_archive(
        project_dir / ".pio" / "controller-cache" / "esp32-bt-lib"
    ).resolve()
except Exception as exc:
    print(f"*** ESP32 controller master A/B archive unavailable: {exc}")
    env.Exit(1)

# The framework links the controller through a single `-lbtdm_app` flag that
# ld resolves over LIBPATH. Prepending the verified project-local cache
# directory shadows the stock framework archive; it is never opened.
libs = list(env.get("LIBS", []))
controller_indexes = [
    index for index, lib in enumerate(libs) if is_controller_library_entry(lib)
]
if len(controller_indexes) != 1:
    print(
        "*** ESP32 controller master A/B failed: expected exactly one framework btdm_app entry, "
        f"found {len(controller_indexes)}"
    )
    env.Exit(1)
env.Prepend(LIBPATH=[str(controller_archive.parent)])

link_map = Path(env.subst("$BUILD_DIR")) / "controller-link.map"
env.Append(LINKFLAGS=[f"-Wl,-Map,{link_map}"])


def verify_controller_link(source, target, env):
    del source, target
    try:
        text = link_map.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        print(f"*** ESP32 controller master A/B link-map check failed: {exc}")
        env.Exit(1)
        return

    pulled = controller_archive_member_paths(text)
    # The linker renders the archive path exactly as given on the command
    # line, which may differ from this script's absolute rendering. The
    # commit-hash cache directory is unique to the pinned archive.
    override_members = [line for line in pulled if CONTROLLER_COMMIT in line]
    stock_members = [line for line in pulled if CONTROLLER_COMMIT not in line]
    if not override_members and pulled:
        print(
            f"*** ESP32 controller master A/B: map has {len(pulled)} libbtdm_app.a "
            "member lines, none from the pinned archive; first lines:"
        )
        for line in pulled[:10]:
            print(f"***   {line}")

    if not override_members:
        print(
            "*** ESP32 controller master A/B failed: pinned libbtdm_app.a contributed no linked members"
        )
        env.Exit(1)
        return
    if stock_members:
        print(
            "*** ESP32 controller master A/B failed: stock libbtdm_app.a also contributed linked members"
        )
        for line in stock_members[:5]:
            print(f"***   {line}")
        env.Exit(1)
        return

    print(
        "*** ESP32 controller master A/B linked "
        f"{CONTROLLER_COMMIT} (blob {CONTROLLER_BLOB_SHA1})"
    )


env.AddPostAction("$PROGPATH", verify_controller_link)
print(
    "*** ESP32 controller master A/B: overriding libbtdm_app.a with "
    f"{CONTROLLER_COMMIT} (blob {CONTROLLER_BLOB_SHA1})"
)
