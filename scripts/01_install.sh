#!/bin/bash

echo "1. Install host dependencies"
sudo apt install make gcc python3 python3-pip python3-lz4 git git-lfs wget build-essential meson ninja-build libre2-dev pybind11-dev texlive-full -y
pip3 install matplotlib numpy pandas requests seaborn