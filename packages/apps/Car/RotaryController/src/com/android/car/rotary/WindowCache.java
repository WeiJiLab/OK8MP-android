/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.android.car.rotary;

import androidx.annotation.Nullable;

import java.util.HashMap;
import java.util.Map;
import java.util.Stack;

/** Cache of window IDs and types. */
class WindowCache {
    /** Window IDs. */
    private final Stack<Integer> mWindowIds;
    /** Window types keyed by window IDs. */
    private final Map<Integer, Integer> mWindowTypes;

    WindowCache() {
        mWindowIds = new Stack<>();
        mWindowTypes = new HashMap<>();
    }

    /** Puts an entry, removing the existing entry, if any. */
    void put(int windowId, int windowType) {
        Integer id = windowId;
        if (mWindowIds.contains(id)) {
            // Call remove(Integer) to remove.
            mWindowIds.remove(id);
        }
        mWindowIds.push(id);

        mWindowTypes.put(windowId, windowType);
    }

    /** Removes an entry if it exists. */
    void remove(int windowId) {
        Integer id = windowId;
        if (mWindowIds.contains(id)) {
            // Call remove(Integer) to remove.
            mWindowIds.remove(id);
            mWindowTypes.remove(id);
        }
    }

    /** Gets the window type keyed by {@code windowId}, or null if none. */
    @Nullable
    Integer getWindowType(int windowId) {
        return mWindowTypes.get(windowId);
    }

    /** Gets the most recently added window ID, or null if none. */
    @Nullable
    Integer getMostRecentWindowId() {
        if (mWindowIds.isEmpty()) {
            return null;
        }
        return mWindowIds.peek();
    }
}
