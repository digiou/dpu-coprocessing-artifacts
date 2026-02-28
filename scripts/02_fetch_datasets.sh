#!/bin/bash

echo "2. Fetch datasets..."
cd ../.. && git submodule update --recursive # in local-compress/corpora/silesia
cd experiments/regex && mkdir -p data && cd data
curl -L -o US_Accidents_Dec21_updated.csv https://huggingface.co/datasets/nateraw/us-accidents/resolve/main/US_Accidents_Dec21_updated.csv
unzip us-accidents.zip && rm -rf us-accidents.zip # in regex/data/US_Accidents_Dec21_updated.csv