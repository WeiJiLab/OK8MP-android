#!/bin/bash
# Copyright 2020 - The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the',  help='License');
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an',  help='AS IS' BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Modifies an input manifest.xml (such as x86_64-linux-gnu/manifest.xml)
# to point to a particular Android build id of gfxstream

BUILD_ID=$1
INPUT_MANIFEST=$2

/google/data/ro/projects/android/fetch_artifact \
    --bid ${BUILD_ID} \
    --target gfxstream_sdk_tools_linux "manifest_${BUILD_ID}.xml"

# Check the output and use it to replace the INPUT_MANIFEST
python update-manifest-gfxstream.py \
    manifest_${BUILD_ID}.xml \
    ${INPUT_MANIFEST}
