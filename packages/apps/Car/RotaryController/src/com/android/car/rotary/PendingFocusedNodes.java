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

import android.os.SystemClock;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;

/**
 * Cache of nodes that have performed {@link AccessibilityNodeInfo#ACTION_FOCUS} but haven't
 * reported a {@link AccessibilityEvent#TYPE_VIEW_FOCUSED} event yet.
 */
class PendingFocusedNodes {
    /**
     * The map to store the nodes.
     * <p>
     * The key of an entry is the node. The value of an entry is its expire time, which is the time
     * when it's added (in {@link SystemClock#uptimeMillis}) plus {@link #mTimeoutMs}.
     * <p>
     * An entry is added when the node just performed the focus action successfully, and it's
     * removed when we receive the focus event for the node.
     */
    private final Map<AccessibilityNodeInfo, Long> mMap = new HashMap();

    /** How many milliseconds will an entry expire after it's put in the cache. */
    private long mTimeoutMs;

    @NonNull
    private NodeCopier mNodeCopier = new NodeCopier();

    PendingFocusedNodes(long timeoutMs) {
        mTimeoutMs = timeoutMs;
    }

    /** Returns whether the {@code node} is in the cache. */
    boolean contains(@NonNull AccessibilityNodeInfo node) {
        refresh();
        return mMap.containsKey(node);
    }

    /** Puts a copy of the {@code node} in the cache. */
    void put(@NonNull AccessibilityNodeInfo node) {
        mMap.put(copyNode(node), getUptimeMs() + mTimeoutMs);
    }

    /** Returns whether the cache is empty. */
    boolean isEmpty() {
        refresh();
        return mMap.isEmpty();
    }

    /**
     * Searches for a node in the cache satisfying the given condition. If succeeded, removes the
     * first found node and returns true. Otherwise returns false.
     */
    boolean removeFirstIf(@NonNull NodePredicate targetPredicate) {
        for (Map.Entry<AccessibilityNodeInfo, Long> entry : mMap.entrySet()) {
            AccessibilityNodeInfo node = entry.getKey();
            if (targetPredicate.isTarget(node)) {
                L.d("A node in mPendingFocusedNodes was removed");
                mMap.remove(node);
                node.recycle();
                return true;
            }
        }
        return false;
    }

    /**
     * Removes nodes saved in the cache if they're not in the view tree any more, or they're
     * expired.
     */
    @VisibleForTesting
    void refresh() {
        long uptimeMs = getUptimeMs();
        Iterator<Map.Entry<AccessibilityNodeInfo, Long>> iterator = mMap.entrySet().iterator();
        while (iterator.hasNext()) {
            Map.Entry<AccessibilityNodeInfo, Long> entry = iterator.next();
            AccessibilityNodeInfo node = entry.getKey();
            if (!node.refresh()) {
                L.d("Pending focused node is not in view tree: " + node);
                iterator.remove();
                node.recycle();
                continue;
            }
            long expireTimeMs = entry.getValue();
            if (uptimeMs > expireTimeMs) {
                L.d("Pending focused node is expired: " + node);
                iterator.remove();
                node.recycle();
            }
        }
    }

    @VisibleForTesting
    long getUptimeMs() {
        return SystemClock.uptimeMillis();
    }

    /** Sets a mock Utils instance for testing. */
    @VisibleForTesting
    void setNodeCopier(@NonNull NodeCopier nodeCopier) {
        mNodeCopier = nodeCopier;
    }

    /** Returns the size of the cache. */
    @VisibleForTesting
    int size() {
        return mMap.size();
    }

    private AccessibilityNodeInfo copyNode(@Nullable AccessibilityNodeInfo node) {
        return mNodeCopier.copy(node);
    }
}
