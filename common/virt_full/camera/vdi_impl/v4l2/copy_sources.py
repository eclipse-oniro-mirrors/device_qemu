#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Device Co., Ltd.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Keep the existing camera source deployment without rewriting equal files."""

import argparse
import hashlib
import os
from pathlib import Path
import shutil


def escape(path):
    value = os.path.relpath(path)
    if "\n" in value or "\r" in value:
        raise ValueError("Dependency paths cannot contain newlines")
    return (value.replace("\\", "\\\\").replace("$", "$$")
            .replace(" ", "\\ ").replace("\t", "\\\t")
            .replace("#", "\\#").replace(":", "\\:"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sources", nargs="+", required=True)
    parser.add_argument("--destination", required=True)
    parser.add_argument("--stamp", required=True)
    parser.add_argument("--depfile", required=True)
    args = parser.parse_args()
    destination = Path(args.destination)
    destination.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256()
    dependencies = [Path(__file__)]
    copied = False
    # Inputs and filenames come from hc-gen's declared outputs, not a fixed list.
    for source in map(Path, args.sources):
        content = source.read_bytes()
        target = destination / source.name
        if not target.is_file() or target.read_bytes() != content:
            shutil.copyfile(source, target)
            copied = True
        digest.update(source.name.encode("utf-8") + b"\0" + content + b"\0")
        dependencies.extend((source, target))
    depfile = Path(args.depfile)
    depfile.parent.mkdir(parents=True, exist_ok=True)
    depfile.write_text(escape(args.stamp) + ": " + " ".join(
        escape(path) for path in dependencies) + "\n", encoding="utf-8")
    stamp = Path(args.stamp)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    signature = digest.hexdigest() + "\n"
    if not stamp.is_file() or stamp.read_text(encoding="utf-8") != signature:
        stamp.write_text(signature, encoding="utf-8")
    elif copied:
        # A different product may have replaced the deployed files. Acknowledge
        # their new mtimes after restoring this product's identical signature.
        stamp.touch()


if __name__ == "__main__":
    main()
