/*
 * Copyright 2020 The Android Open Source Project
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

import static com.google.common.truth.Truth.assertThat;

import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;

import android.view.accessibility.AccessibilityNodeInfo;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Spy;
import org.robolectric.RobolectricTestRunner;

import java.util.ArrayList;

@RunWith(RobolectricTestRunner.class)
public class PendingFocusedNodesTest {
    private static final long TIMEOUT_MS = 200;
    private static final long UPTIME_MS = 0;

    @Spy
    private PendingFocusedNodes mPendingFocusedNodes;

    private NodeBuilder mNodeBuilder;

    private AccessibilityNodeInfo mNode1;
    private AccessibilityNodeInfo mNode2;

    @Before
    public void setUp() {
        mPendingFocusedNodes = spy(new PendingFocusedNodes(TIMEOUT_MS));
        doReturn(UPTIME_MS).when(mPendingFocusedNodes).getUptimeMs();
        mPendingFocusedNodes.setNodeCopier(MockNodeCopierProvider.get());

        mNodeBuilder = new NodeBuilder(new ArrayList<>());

        mNode1 = mNodeBuilder.build();
        mNode2 = mNodeBuilder.build();
    }

    @Test
    public void testContains() {
        assertThat(mPendingFocusedNodes.contains(mNode1)).isFalse();

        mPendingFocusedNodes.put(mNode1);
        assertThat(mPendingFocusedNodes.contains(mNode1)).isTrue();
        assertThat(mPendingFocusedNodes.contains(mNode2)).isFalse();
    }

    @Test
    public void testIsEmpty() {
        assertThat(mPendingFocusedNodes.isEmpty()).isTrue();

        mPendingFocusedNodes.put(mNode1);
        assertThat(mPendingFocusedNodes.isEmpty()).isFalse();
    }

    @Test
    public void testRefreshDoesNotRemoveNode() {
        mPendingFocusedNodes.put(mNode1);
        mPendingFocusedNodes.refresh();
        assertThat(mPendingFocusedNodes.contains(mNode1)).isTrue();
    }

    @Test
    public void testRefreshRemovesExpiredNode() {
        mPendingFocusedNodes.put(mNode1);
        when(mPendingFocusedNodes.getUptimeMs()).thenReturn(UPTIME_MS + TIMEOUT_MS + 1);
        mPendingFocusedNodes.refresh();
        assertThat(mPendingFocusedNodes.isEmpty()).isTrue();
    }

    @Test
    public void testRefreshRemovesNodeNotInViewTree() {
        AccessibilityNodeInfo node = mNodeBuilder.setInViewTree(false).build();
        mPendingFocusedNodes.put(node);
        mPendingFocusedNodes.refresh();
        assertThat(mPendingFocusedNodes.isEmpty()).isTrue();
    }

    @Test
    public void testRemoveIf() {
        mPendingFocusedNodes.put(mNode1);
        AccessibilityNodeInfo disabled1 = mNodeBuilder.setEnabled(false).build();
        AccessibilityNodeInfo disabled2 = mNodeBuilder.setEnabled(false).build();
        mPendingFocusedNodes.put(disabled1);
        mPendingFocusedNodes.put(disabled2);

        boolean removed = mPendingFocusedNodes.removeFirstIf(node -> !node.isEnabled());
        assertThat(removed).isTrue();
        assertThat(mPendingFocusedNodes.size()).isEqualTo(2);
        assertThat(mPendingFocusedNodes.contains(mNode1)).isTrue();

        removed = mPendingFocusedNodes.removeFirstIf(node -> !node.isEnabled());
        assertThat(removed).isTrue();
        assertThat(mPendingFocusedNodes.size()).isEqualTo(1);
        assertThat(mPendingFocusedNodes.contains(disabled1)).isFalse();
        assertThat(mPendingFocusedNodes.contains(disabled2)).isFalse();
        assertThat(mPendingFocusedNodes.contains(mNode1)).isTrue();

        removed = mPendingFocusedNodes.removeFirstIf(node -> !node.isFocusable());
        assertThat(removed).isFalse();
        assertThat(mPendingFocusedNodes.contains(mNode1)).isTrue();
    }
}
