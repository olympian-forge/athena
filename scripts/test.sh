#!/bin/bash

#   Copyright (c) 2026 Ike
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <https://www.gnu.org/licenses/>.

set -e

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

VENV_DIR="$PROJECT_ROOT/.venv"
FIXTURE_REQUIREMENTS="$PROJECT_ROOT/tests/tools/requirements.txt"
FIXTURE_GENERATOR="$PROJECT_ROOT/tests/tools/make_test_model.py"
FIXTURES=("$PROJECT_ROOT/tests/dummy.onnx" "$PROJECT_ROOT/tests/dummy_wrong_policy.onnx")

ensure_python() {
    if command -v python3 >/dev/null 2>&1; then
        return
    fi

    echo "python3 not found; attempting to install it..."
    if command -v apt-get >/dev/null 2>&1; then
        local sudo_cmd=""
        if [ "$(id -u)" -ne 0 ]; then
            sudo_cmd="sudo"
        fi
        $sudo_cmd apt-get update
        $sudo_cmd apt-get install -y python3 python3-venv python3-pip
    else
        echo "ERROR: python3 is required to generate the ONNX test fixtures, and" >&2
        echo "       no apt-get is available to install it. Install python3 and retry." >&2
        exit 1
    fi
}

generate_fixtures() {
    ensure_python

    if [ ! -d "$VENV_DIR" ]; then
        echo "Creating $VENV_DIR for fixture generation..."
        python3 -m venv "$VENV_DIR"
    fi

    # shellcheck disable=SC1091
    source "$VENV_DIR/bin/activate"

    if ! python3 -c "import onnx, numpy" >/dev/null 2>&1; then
        echo "Installing fixture dependencies (onnx, numpy)..."
        python3 -m pip install --quiet --upgrade pip
        python3 -m pip install --quiet -r "$FIXTURE_REQUIREMENTS"
    fi

    echo "Generating ONNX test fixtures..."
    python3 "$FIXTURE_GENERATOR" --out-dir "$PROJECT_ROOT/tests"

    for fixture in "${FIXTURES[@]}"; do
        if [ ! -s "$fixture" ]; then
            echo "ERROR: expected fixture $fixture was not created." >&2
            exit 1
        fi
    done

    deactivate
}

run_tests() {
    (cd "$PROJECT_ROOT" && \
     generate_fixtures && \
     rm -rf build && \
     cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTING=ON && \
     cmake --build build -j $(nproc) && \
     cmake --build build --target coverage)
}

run_tests
