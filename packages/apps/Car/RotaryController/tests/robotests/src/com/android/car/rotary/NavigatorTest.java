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

import static android.view.accessibility.AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_FORWARD;

import static com.google.common.truth.Truth.assertThat;

import static org.mockito.Mockito.when;

import android.graphics.Rect;
import android.view.View;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityWindowInfo;

import androidx.annotation.NonNull;

import com.android.car.rotary.Navigator.FindRotateTargetResult;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

import java.util.ArrayList;
import java.util.List;

@RunWith(RobolectricTestRunner.class)
public class NavigatorTest {

    private Rect mHunWindowBounds;
    private NodeBuilder mNodeBuilder;
    private Navigator mNavigator;

    @Before
    public void setUp() {
        mHunWindowBounds = new Rect(50, 10, 950, 200);
        mNodeBuilder = new NodeBuilder(new ArrayList<>());
        mNavigator = new Navigator(
                /* focusHistoryCacheType= */ RotaryCache.CACHE_TYPE_NEVER_EXPIRE,
                /* focusHistoryCacheSize= */ 10,
                /* focusHistoryExpirationTimeMs= */ 0,
                /* focusAreaHistoryCacheType= */ RotaryCache.CACHE_TYPE_NEVER_EXPIRE,
                /* focusAreaHistoryCacheSize= */ 5,
                /* focusAreaHistoryExpirationTimeMs= */ 0,
                /* focusWindowCacheType= */ RotaryCache.CACHE_TYPE_NEVER_EXPIRE,
                /* focusWindowCacheSize= */ 5,
                /* focusWindowExpirationTimeMs= */ 0,
                mHunWindowBounds.left,
                mHunWindowBounds.right,
                /* showHunOnBottom= */ false);
        mNavigator.setNodeCopier(MockNodeCopierProvider.get());
    }

    @Test
    public void testSetRootNodeForWindow() {
        AccessibilityWindowInfo window = new WindowBuilder().build();
        AccessibilityNodeInfo root = mNodeBuilder.build();
        setRootNodeForWindow(root, window);

        assertThat(window.getRoot()).isSameAs(root);
    }

    /**
     * Tests {@link Navigator#findRotateTarget} in the following node tree:
     * <pre>
     *              root
     *               |
     *           focusArea
     *          /    |    \
     *        /      |     \
     *    button1 button2 button3
     * </pre>
     */
    @Test
    public void testFindRotateTarget() {
        AccessibilityNodeInfo root = mNodeBuilder.build();
        AccessibilityNodeInfo focusArea = mNodeBuilder.setParent(root).setFocusArea().build();

        AccessibilityNodeInfo button1 = mNodeBuilder.setParent(focusArea).build();
        AccessibilityNodeInfo button2 = mNodeBuilder.setParent(focusArea).build();
        AccessibilityNodeInfo button3 = mNodeBuilder.setParent(focusArea).build();

        int direction = View.FOCUS_FORWARD;
        when(button1.focusSearch(direction)).thenReturn(button2);
        when(button2.focusSearch(direction)).thenReturn(button3);
        when(button3.focusSearch(direction)).thenReturn(null);

        // Rotate once, the focus should move from button1 to button2.
        FindRotateTargetResult target = mNavigator.findRotateTarget(button1, direction, 1);
        assertThat(target.node).isSameAs(button2);
        assertThat(target.advancedCount).isEqualTo(1);

        // Rotate twice, the focus should move from button1 to button3.
        target = mNavigator.findRotateTarget(button1, direction, 2);
        assertThat(target.node).isSameAs(button3);
        assertThat(target.advancedCount).isEqualTo(2);

        // Rotate 3 times and exceed the boundary, the focus should stay at the boundary.
        target = mNavigator.findRotateTarget(button1, direction, 3);
        assertThat(target.node).isSameAs(button3);
        assertThat(target.advancedCount).isEqualTo(2);
    }

    /**
     * Tests {@link Navigator#findRotateTarget} in the following node tree:
     * <pre>
     *                     root
     *                    /    \
     *                   /      \
     *      focusParkingView   focusArea
     *                           /    \
     *                          /      \
     *                       button1  button2
     * </pre>
     */
    @Test
    public void testFindRotateTargetNoWrapAround() {
        AccessibilityNodeInfo root = mNodeBuilder.build();
        AccessibilityNodeInfo focusParkingView = mNodeBuilder.setParent(root).setFpv().build();
        AccessibilityNodeInfo focusArea = mNodeBuilder.setParent(root).setFocusArea().build();

        AccessibilityNodeInfo button1 = mNodeBuilder.setParent(focusArea).build();
        AccessibilityNodeInfo button2 = mNodeBuilder.setParent(focusArea).build();

        int direction = View.FOCUS_FORWARD;
        when(button1.focusSearch(direction)).thenReturn(button2);
        when(button2.focusSearch(direction)).thenReturn(focusParkingView);
        when(focusParkingView.focusSearch(direction)).thenReturn(button1);

        // Rotate at the end of focus area, no wrap-around should happen.
        FindRotateTargetResult target = mNavigator.findRotateTarget(button2, direction, 1);
        assertThat(target).isNull();
    }

    /**
     * Tests {@link Navigator#findRotateTarget} in the following node tree:
     * <pre>
     *                          root
     *                         /  |  \
     *                       /    |    \
     *                     /      |      \
     *      focusParkingView   button1   button2
     * </pre>
     */
    @Test
    public void testFindRotateTargetNoWrapAround2() {
        AccessibilityNodeInfo root = mNodeBuilder.build();
        AccessibilityNodeInfo focusParkingView = mNodeBuilder.setParent(root).setFpv().build();
        AccessibilityNodeInfo button1 = mNodeBuilder.setParent(root).build();
        AccessibilityNodeInfo button2 = mNodeBuilder.setParent(root).build();

        int direction = View.FOCUS_FORWARD;
        when(button1.focusSearch(direction)).thenReturn(button2);
        when(button2.focusSearch(direction)).thenReturn(focusParkingView);
        when(focusParkingView.focusSearch(direction)).thenReturn(button1);

        // Rotate at the end of focus area, no wrap-around should happen.
        FindRotateTargetResult target = mNavigator.findRotateTarget(button2, direction, 1);
        assertThat(target).isNull();
    }

    /**
     * Tests {@link Navigator#findRotateTarget} in the following node tree:
     * <pre>
     *                          root
     *                         /  |  \
     *                       /    |    \
     *                     /      |      \
     *               button1   invisible   button2
     * </pre>
     */
    @Test
    public void testFindRotateTargetDoesNotSkipInvisibleNode() {
        AccessibilityNodeInfo root = mNodeBuilder.build();
        AccessibilityNodeInfo button1 = mNodeBuilder.setParent(root).build();
        AccessibilityNodeInfo invisible = mNodeBuilder
                .setParent(root)
                .setVisibleToUser(false)
                .setBoundsInScreen(new Rect(0, 0, 0, 0))
                .build();
        AccessibilityNodeInfo button2 = mNodeBuilder.setParent(root).build();

        int direction = View.FOCUS_FORWARD;
        when(button1.focusSearch(direction)).thenReturn(invisible);
        when(invisible.focusSearch(direction)).thenReturn(button2);

        // Rotate from button1, it shouldn't skip the invisible view.
        FindRotateTargetResult target = mNavigator.findRotateTarget(button1, direction, 1);
        assertThat(target.node).isSameAs(invisible);
    }

    /**
     * Tests {@link Navigator#findRotateTarget} in the following node tree:
     * <pre>
     *                          root
     *                         /  |  \
     *                       /    |    \
     *                     /      |      \
     *               button1   empty   button2
     * </pre>
     */
    @Test
    public void testFindRotateTargetSkipNodeThatCannotPerformFocus() {
        AccessibilityNodeInfo root = mNodeBuilder.build();
        AccessibilityNodeInfo button1 = mNodeBuilder.setParent(root).build();
        AccessibilityNodeInfo empty = mNodeBuilder
                .setParent(root)
                .setBoundsInParent(new Rect(0, 0, 0, 0))
                .build();
        AccessibilityNodeInfo button2 = mNodeBuilder.setParent(root).build();

        int direction = View.FOCUS_FORWARD;
        when(button1.focusSearch(direction)).thenReturn(empty);
        when(empty.focusSearch(direction)).thenReturn(button2);

        // Rotate from button1, it should skip the empty view.
        FindRotateTargetResult target = mNavigator.findRotateTarget(button1, direction, 1);
        assertThat(target.node).isSameAs(button2);
    }

    /**
     * Tests {@link Navigator#findRotateTarget} in the following node tree:
     * <pre>
     *                          root
     *                         /  |  \
     *                       /    |    \
     *                     /      |      \
     *              button1   scrollable  button2
     *                       recyclerView
     *                            |
     *                      non-focusable
     * </pre>
     */
    @Test
    public void testFindRotateTargetReturnScrollableContainer() {
        AccessibilityNodeInfo root = mNodeBuilder.build();
        AccessibilityNodeInfo button1 = mNodeBuilder.setParent(root).build();
        AccessibilityNodeInfo button2 = mNodeBuilder.setParent(root).build();
        AccessibilityNodeInfo recyclerView = mNodeBuilder
                .setParent(root)
                .setScrollableContainer()
                .setScrollable(true)
                .build();
        AccessibilityNodeInfo nonFocusable = mNodeBuilder
                .setFocusable(false)
                .setParent(recyclerView)
                .build();

        int direction = View.FOCUS_FORWARD;
        when(button1.focusSearch(direction)).thenReturn(recyclerView);
        when(recyclerView.focusSearch(direction)).thenReturn(button2);

        FindRotateTargetResult target = mNavigator.findRotateTarget(button1, direction, 1);
        assertThat(target.node).isSameAs(recyclerView);
    }

    /**
     * Tests {@link Navigator#findRotateTarget} in the following node tree:
     * <pre>
     *                          root
     *                         /  |  \
     *                       /    |    \
     *                     /      |      \
     *                   /        |        \
     *             button1  non-scrollable  button2
     *                       recyclerView
     * </pre>
     */
    @Test
    public void testFindRotateTargetSkipScrollableContainer() {
        AccessibilityNodeInfo root = mNodeBuilder.build();
        AccessibilityNodeInfo button1 = mNodeBuilder.setParent(root).build();
        AccessibilityNodeInfo button2 = mNodeBuilder.setParent(root).build();
        AccessibilityNodeInfo recyclerView = mNodeBuilder
                .setParent(root)
                .setScrollableContainer()
                .build();

        int direction = View.FOCUS_FORWARD;
        when(button1.focusSearch(direction)).thenReturn(recyclerView);
        when(recyclerView.focusSearch(direction)).thenReturn(button2);

        FindRotateTargetResult target = mNavigator.findRotateTarget(button1, direction, 1);
        assertThat(target.node).isSameAs(button2);
    }

    /**
     * Tests {@link Navigator#findRotateTarget} in the following node tree:
     * <pre>
     *                     root
     *                    /    \
     *                  /       \
     *    focusParkingView    scrollable
     *                       recyclerView
     *                           /    \
     *                          /      \
     *                  focusable1    focusable2
     * </pre>
     */
    @Test
    public void testFindRotateTargetSkipScrollableContainer2() {
        AccessibilityNodeInfo root = mNodeBuilder.build();
        AccessibilityNodeInfo focusParkingView = mNodeBuilder.setParent(root).setFpv().build();
        AccessibilityNodeInfo recyclerView = mNodeBuilder
                .setParent(root)
                .setScrollableContainer()
                .setScrollable(true)
                .build();
        AccessibilityNodeInfo focusable1 = mNodeBuilder.setParent(recyclerView).build();
        AccessibilityNodeInfo focusable2 = mNodeBuilder.setParent(recyclerView).build();

        int direction = View.FOCUS_BACKWARD;
        when(focusable2.focusSearch(direction)).thenReturn(focusable1);
        when(focusable1.focusSearch(direction)).thenReturn(recyclerView);
        when(recyclerView.focusSearch(direction)).thenReturn(focusParkingView);

        FindRotateTargetResult target = mNavigator.findRotateTarget(focusable2, direction, 2);
        assertThat(target.node).isSameAs(focusable1);
        assertThat(target.advancedCount).isEqualTo(1);
    }

    /**
     * Tests {@link Navigator#findRotateTarget} in the following node tree:
     * <pre>
     *             node
     * </pre>
     */
    @Test
    public void testFindRotateTargetWithOneNode() {
        AccessibilityNodeInfo node = mNodeBuilder.build();
        int direction = View.FOCUS_BACKWARD;
        when(node.focusSearch(direction)).thenReturn(node);

        FindRotateTargetResult target = mNavigator.findRotateTarget(node, direction, 1);
        assertThat(target).isNull();
    }

    /**
     * Tests {@link Navigator#findRotateTarget} in the following layout:
     * <pre>
     *     ============ focus area ============
     *     =                                  =
     *     =  ***** scrollable container **** =
     *     =  *                             * =
     *     =  *  ........ button 1 ........ * =
     *     =  *  .                        . * =
     *     =  *  .......................... * =
     *     =  *                             * =
     *     =  *  ........ button 2 ........ * =
     *     =  *  .                        . * =
     *     =  *  .......................... * =
     *     =  *                             * =
     *     =  ******************************* =
     *     =                                  =
     *     ============ focus area ============
     *
     *           ........ button 3 ........
     *           .                        .
     *           ..........................
     * </pre>
     * where {@code button 3} is not a descendant of the scrollable container.
     */
    @Test
    public void testFindRotateTargetInScrollableContainer() {
        AccessibilityNodeInfo root = mNodeBuilder.build();
        AccessibilityNodeInfo focusArea = mNodeBuilder
                .setParent(root)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 0, 100, 100))
                .build();
        AccessibilityNodeInfo scrollableContainer = mNodeBuilder
                .setParent(focusArea)
                .setScrollableContainer()
                .setActions(ACTION_SCROLL_FORWARD)
                .setBoundsInScreen(new Rect(0, 0, 100, 100))
                .build();

        AccessibilityNodeInfo button1 = mNodeBuilder
                .setParent(scrollableContainer)
                .setBoundsInScreen(new Rect(0, 0, 100, 50))
                .build();
        AccessibilityNodeInfo button2 = mNodeBuilder
                .setParent(scrollableContainer)
                .setBoundsInScreen(new Rect(0, 50, 100, 100))
                .build();
        AccessibilityNodeInfo button3 = mNodeBuilder
                .setParent(root)
                .setBoundsInScreen(new Rect(0, 100, 100, 150))
                .build();

        int direction = View.FOCUS_FORWARD;
        when(button1.focusSearch(direction)).thenReturn(button2);
        when(button2.focusSearch(direction)).thenReturn(button3);
        when(button3.focusSearch(direction)).thenReturn(null);

        // Rotate once, the focus should move from button1 to button2.
        FindRotateTargetResult target = mNavigator.findRotateTarget(button1, direction, 1);
        assertThat(target.node).isSameAs(button2);
        assertThat(target.advancedCount).isEqualTo(1);

        // Rotate twice, the focus should move from button1 to button2 since button3 is not a
        // descendant of the scrollable container.
        target = mNavigator.findRotateTarget(button1, direction, 2);
        assertThat(target.node).isSameAs(button2);
        assertThat(target.advancedCount).isEqualTo(1);

        // Rotate three times should do the same.
        target = mNavigator.findRotateTarget(button1, direction, 3);
        assertThat(target.node).isSameAs(button2);
        assertThat(target.advancedCount).isEqualTo(1);
    }

    /**
     * Tests {@link Navigator#findRotateTarget} in the following layout:
     * <pre>
     *     ============ focus area ============
     *     =                                  =
     *     =  ***** scrollable container **** =
     *     =  *                             * =
     *     =  *  ........ button 1 ........ * =
     *     =  *  .                        . * =
     *     =  *  .......................... * =
     *     =  *                             * =
     *     =  *  ........ button 2 ........ * =
     *     =  *  .                        . * =
     *     =  *  .......................... * =
     *     =  *                             * =
     *     =  ******************************* =
     *     =                                  =
     *     ============ focus area ============
     *
     *           ........ button 3 ........
     *           .                        .
     *           ..........................
     * </pre>
     * where {@code button 3} is off the screen.
     */
    @Test
    public void testFindRotateTargetInScrollableContainer2() {
        AccessibilityNodeInfo root = mNodeBuilder.build();
        AccessibilityNodeInfo focusArea = mNodeBuilder
                .setParent(root)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 0, 100, 100))
                .build();
        AccessibilityNodeInfo scrollableContainer = mNodeBuilder
                .setParent(focusArea)
                .setScrollableContainer()
                .setActions(ACTION_SCROLL_FORWARD)
                .setBoundsInScreen(new Rect(0, 0, 100, 100))
                .build();

        AccessibilityNodeInfo button1 = mNodeBuilder
                .setParent(scrollableContainer)
                .setBoundsInScreen(new Rect(0, 0, 100, 50))
                .build();
        AccessibilityNodeInfo button2 = mNodeBuilder
                .setParent(scrollableContainer)
                .setBoundsInScreen(new Rect(0, 50, 100, 100))
                .build();
        AccessibilityNodeInfo button3 = mNodeBuilder
                .setParent(root)
                .setBoundsInScreen(new Rect(0, 0, 0, 0))
                .build();

        int direction = View.FOCUS_FORWARD;
        when(button1.focusSearch(direction)).thenReturn(button2);
        when(button2.focusSearch(direction)).thenReturn(button3);
        when(button3.focusSearch(direction)).thenReturn(null);

        // Rotate once, the focus should move from button1 to button2.
        FindRotateTargetResult target = mNavigator.findRotateTarget(button1, direction, 1);
        assertThat(target.node).isSameAs(button2);
        assertThat(target.advancedCount).isEqualTo(1);

        // Rotate twice, the focus should move from button1 to button2 since button3 is off the
        // screen.
        target = mNavigator.findRotateTarget(button1, direction, 2);
        assertThat(target.node).isSameAs(button2);
        assertThat(target.advancedCount).isEqualTo(1);

        // Rotate three times should do the same.
        target = mNavigator.findRotateTarget(button1, direction, 3);
        assertThat(target.node).isSameAs(button2);
        assertThat(target.advancedCount).isEqualTo(1);
    }

    /**
     * Tests {@link Navigator#findNudgeTarget} in the following layout:
     * <pre>
     *    ****************leftWindow**************    **************rightWindow****************
     *    *                                      *    *                                       *
     *    *  ========topLeft focus area========  *    *  ========topRight focus area========  *
     *    *  =                                =  *    *  =                                 =  *
     *    *  =  .............  .............  =  *    *  =  .............                  =  *
     *    *  =  .           .  .           .  =  *    *  =  .           .                  =  *
     *    *  =  . topLeft1  .  .  topLeft2 .  =  *    *  =  . topRight1 .                  =  *
     *    *  =  .           .  .           .  =  *    *  =  .           .                  =  *
     *    *  =  .............  .............  =  *    *  =  .............                  =  *
     *    *  =                                =  *    *  =                                 =  *
     *    *  ==================================  *    *  ===================================  *
     *    *                                      *    *                                       *
     *    *  =======middleLeft focus area======  *    *                                       *
     *    *  =                                =  *    *                                       *
     *    *  =  .............  .............  =  *    *                                       *
     *    *  =  .           .  .           .  =  *    *                                       *
     *    *  =  .middleLeft1.  .middleLeft2.  =  *    *                                       *
     *    *  =  . disabled  .  . disabled  .  =  *    *                                       *
     *    *  =  .............  .............  =  *    *                                       *
     *    *  =                                =  *    *                                       *
     *    *  ==================================  *    *                                       *
     *    *                                      *    *                                       *
     *    *  =======bottomLeft focus area======  *    *                                       *
     *    *  =                                =  *    *                                       *
     *    *  =  .............  .............  =  *    *                                       *
     *    *  =  .           .  .           .  =  *    *                                       *
     *    *  =  .bottomLeft1.  .bottomLeft2.  =  *    *                                       *
     *    *  =  .           .  .           .  =  *    *                                       *
     *    *  =  .............  .............  =  *    *                                       *
     *    *  =                                =  *    *                                       *
     *    *  ==================================  *    *                                       *
     *    *                                      *    *                                       *
     *    ****************************************    *****************************************
     * </pre>
     */
    @Test
    public void testFindNudgeTarget() {
        // There are 2 windows. This is the left window.
        Rect leftWindowBounds = new Rect(0, 0, 400, 1200);
        AccessibilityWindowInfo leftWindow = new WindowBuilder()
                .setBoundsInScreen(leftWindowBounds)
                .build();
        // We must specify window and boundsInScreen for each node when finding nudge target.
        AccessibilityNodeInfo leftRoot = mNodeBuilder
                .setWindow(leftWindow)
                .setBoundsInScreen(leftWindowBounds)
                .build();
        setRootNodeForWindow(leftRoot, leftWindow);

        // Left window has 3 vertically aligned focus areas.
        AccessibilityNodeInfo topLeft = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(leftRoot)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 0, 400, 400))
                .build();
        AccessibilityNodeInfo middleLeft = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(leftRoot)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 400, 400, 800))
                .build();
        AccessibilityNodeInfo bottomLeft = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(leftRoot)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 800, 400, 1200))
                .build();

        // Each focus area but middleLeft has 2 horizontally aligned views that can take focus.
        AccessibilityNodeInfo topLeft1 = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(topLeft)
                .setBoundsInScreen(new Rect(0, 0, 200, 400))
                .build();
        AccessibilityNodeInfo topLeft2 = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(topLeft)
                .setBoundsInScreen(new Rect(200, 0, 400, 400))
                .build();
        AccessibilityNodeInfo bottomLeft1 = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(bottomLeft)
                .setBoundsInScreen(new Rect(0, 800, 200, 1200))
                .build();
        AccessibilityNodeInfo bottomLeft2 = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(bottomLeft)
                .setBoundsInScreen(new Rect(200, 800, 400, 1200))
                .build();

        // middleLeft focus area has 2 disabled views, so that it will be skipped when nudging.
        AccessibilityNodeInfo middleLeft1 = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(bottomLeft)
                .setEnabled(false)
                .setBoundsInScreen(new Rect(0, 400, 200, 800))
                .build();
        AccessibilityNodeInfo middleLeft2 = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(bottomLeft)
                .setEnabled(false)
                .setBoundsInScreen(new Rect(200, 400, 400, 800))
                .build();

        // This is the right window.
        Rect rightWindowBounds = new Rect(400, 0, 800, 1200);
        AccessibilityWindowInfo rightWindow = new WindowBuilder()
                .setBoundsInScreen(rightWindowBounds)
                .build();
        AccessibilityNodeInfo rightRoot = mNodeBuilder
                .setWindow(rightWindow)
                .setBoundsInScreen(rightWindowBounds)
                .build();
        setRootNodeForWindow(rightRoot, rightWindow);

        // Right window has 1 focus area.
        AccessibilityNodeInfo topRight = mNodeBuilder
                .setWindow(rightWindow)
                .setParent(rightRoot)
                .setFocusArea()
                .setBoundsInScreen(new Rect(400, 0, 800, 400))
                .build();

        // The focus area has 1 view that can take focus.
        AccessibilityNodeInfo topRight1 = mNodeBuilder
                .setWindow(rightWindow)
                .setParent(topRight)
                .setBoundsInScreen(new Rect(400, 0, 600, 400))
                .build();

        List<AccessibilityWindowInfo> windows = new ArrayList<>();
        windows.add(leftWindow);
        windows.add(rightWindow);

        // Nudge within the same window.
        AccessibilityNodeInfo target =
                mNavigator.findNudgeTarget(windows, topLeft1, View.FOCUS_DOWN);
        assertThat(target).isSameAs(bottomLeft1);

        // Reach to the boundary.
        target = mNavigator.findNudgeTarget(windows, topLeft1, View.FOCUS_UP);
        assertThat(target).isNull();

        // Nudge to a different window.
        target = mNavigator.findNudgeTarget(windows, topRight1, View.FOCUS_LEFT);
        assertThat(target).isSameAs(topLeft2);

        // When nudging back, the focus should return to the previously focused node within the
        // previous focus area, rather than the geometrically close node or focus area.

        // Firstly, we need to save the focused node.
        mNavigator.saveFocusedNode(bottomLeft1);
        // Then nudge to right.
        target = mNavigator.findNudgeTarget(windows, bottomLeft1, View.FOCUS_RIGHT);
        assertThat(target).isSameAs(topRight1);
        // Then nudge back.
        target = mNavigator.findNudgeTarget(windows, topRight1, View.FOCUS_LEFT);
        assertThat(target).isSameAs(bottomLeft1);
    }

    /**
     * Tests {@link Navigator#findNudgeTarget} in the following layout:
     * <pre>
     *    ****************leftWindow**************    **************rightWindow***************
     *    *                                      *    *                                      *
     *    *  ===left focus area===   parking1    *    *   parking2   ===right focus area===  *
     *    *  =                   =               *    *              =                    =  *
     *    *  =  .............    =               *    *              =  .............     =  *
     *    *  =  .           .    =               *    *              =  .           .     =  *
     *    *  =  .   left    .    =               *    *              =  .   right   .     =  *
     *    *  =  .           .    =               *    *              =  .           .     =  *
     *    *  =  .............    =               *    *              =  .............     =  *
     *    *  =                   =               *    *              =                    =  *
     *    *  =====================               *    *              ======================  *
     *    *                                      *    *                                      *
     *    ****************************************    *****************************************
     * </pre>
     */
    @Test
    public void testFindNudgeTargetWithFocusParkingView() {
        // There are 2 windows. This is the left window.
        Rect leftWindowBounds = new Rect(0, 0, 400, 400);
        AccessibilityWindowInfo leftWindow = new WindowBuilder()
                .setBoundsInScreen(leftWindowBounds)
                .build();
        AccessibilityNodeInfo leftRoot = mNodeBuilder
                .setWindow(leftWindow)
                .setBoundsInScreen(leftWindowBounds)
                .build();
        setRootNodeForWindow(leftRoot, leftWindow);

        // Left focus area and its view inside.
        AccessibilityNodeInfo leftFocusArea = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(leftRoot)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 0, 300, 400))
                .build();
        AccessibilityNodeInfo left = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(leftFocusArea)
                .setBoundsInScreen(new Rect(0, 0, 300, 400))
                .build();

        // Left focus parking view.
        AccessibilityNodeInfo parking1 = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(leftFocusArea)
                .setFpv()
                .setBoundsInScreen(new Rect(350, 0, 351, 1))
                .build();

        // Right window.
        Rect rightWindowBounds = new Rect(400, 0, 800, 400);
        AccessibilityWindowInfo rightWindow = new WindowBuilder()
                .setBoundsInScreen(rightWindowBounds)
                .build();
        AccessibilityNodeInfo rightRoot = mNodeBuilder
                .setWindow(rightWindow)
                .setBoundsInScreen(rightWindowBounds)
                .build();
        setRootNodeForWindow(rightRoot, rightWindow);

        // Right focus area and its view inside.
        AccessibilityNodeInfo rightFocusArea = mNodeBuilder
                .setWindow(rightWindow)
                .setParent(rightRoot)
                .setFocusArea()
                .setBoundsInScreen(new Rect(500, 0, 800, 400))
                .build();
        AccessibilityNodeInfo right = mNodeBuilder
                .setWindow(rightWindow)
                .setParent(rightFocusArea)
                .setBoundsInScreen(new Rect(500, 0, 800, 400))
                .build();

        // Right focus parking view.
        AccessibilityNodeInfo parking2 = mNodeBuilder
                .setWindow(rightWindow)
                .setParent(rightFocusArea)
                .setFpv()
                .setBoundsInScreen(new Rect(450, 0, 451, 1))
                .build();

        List<AccessibilityWindowInfo> windows = new ArrayList<>();
        windows.add(leftWindow);
        windows.add(rightWindow);

        // Nudge from left window to right window.
        AccessibilityNodeInfo target = mNavigator.findNudgeTarget(windows, left, View.FOCUS_RIGHT);
        assertThat(target).isSameAs(right);
    }

    /**
     * Tests {@link Navigator#findNudgeTarget} in the following layout:
     * <pre>
     *    ****************mainWindow**************
     *    *                                      *
     *    *    ==============top=============    *
     *    *    =                            =    *
     *    *    =  ...........  ...........  =    *
     *    *    =  .         .  .         .  =    *
     *    *    =  . topLeft .  . topRight.  =    *
     *    *    =  .         .  .         .  =    *
     *    *    =  ...........  ...........  =    *
     *    *    =                            =    *
     *    *    ==============================    *
     *    *                                      *
     *    *    ============bottom============    *
     *    *    =                            =    *
     *    *    =  ...........  ...........  =    *
     *    *    =  .         .  .         .  =    *
     *    *    =  . bottom  .  . bottom  .  =    *
     *    *    =  .  Left   .  .  Right  .  =    *
     *    *    =  ...........  ...........  =    *
     *    *    =                            =    *
     *    *    ==============================    *
     *    *                                      *
     *    ****************************************
     * </pre>
     * with the HUN overlapping the top of the main window:
     * <pre>
     *       *************hunWindow************
     *       * ..............  .............. *
     *       * .  hunLeft   .  .  hunRight  . *
     *       * ..............  .............. *
     *       **********************************
     * </pre>
     */
    @Test
    public void testFindHunNudgeTarget() {
        // There are two windows. This is the HUN window.
        AccessibilityWindowInfo hunWindow = new WindowBuilder()
                .setBoundsInScreen(mHunWindowBounds)
                .setType(AccessibilityWindowInfo.TYPE_SYSTEM)
                .build();
        // We must specify window and boundsInScreen for each node when finding nudge target.
        AccessibilityNodeInfo hunRoot = mNodeBuilder
                .setWindow(hunWindow)
                .setBoundsInScreen(mHunWindowBounds)
                .setFocusable(false)
                .build();
        setRootNodeForWindow(hunRoot, hunWindow);

        // HUN window has two views that can take focus (directly in the root).
        AccessibilityNodeInfo hunLeft = mNodeBuilder
                .setWindow(hunWindow)
                .setParent(hunRoot)
                .setBoundsInScreen(new Rect(mHunWindowBounds.left, mHunWindowBounds.top,
                        mHunWindowBounds.centerX(), mHunWindowBounds.bottom))
                .build();
        AccessibilityNodeInfo hunRight = mNodeBuilder
                .setWindow(hunWindow)
                .setParent(hunRoot)
                .setBoundsInScreen(new Rect(mHunWindowBounds.centerX(), mHunWindowBounds.top,
                        mHunWindowBounds.right, mHunWindowBounds.bottom))
                .build();

        // This is the main window.
        Rect mainWindowBounds = new Rect(0, 0, 1000, 1000);
        AccessibilityWindowInfo mainWindow = new WindowBuilder()
                .setBoundsInScreen(mainWindowBounds)
                .build();
        AccessibilityNodeInfo mainRoot = mNodeBuilder
                .setWindow(mainWindow)
                .setBoundsInScreen(mainWindowBounds)
                .setFocusable(false)
                .build();
        setRootNodeForWindow(mainRoot, mainWindow);

        // Main window has two focus areas.
        AccessibilityNodeInfo topFocusArea = mNodeBuilder
                .setWindow(mainWindow)
                .setParent(mainRoot)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 0, 1000, 500))
                .build();
        AccessibilityNodeInfo bottomFocusArea = mNodeBuilder
                .setWindow(mainWindow)
                .setParent(mainRoot)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 500, 1000, 1000))
                .build();

        // The top focus area has two views that can take focus.
        AccessibilityNodeInfo topLeft = mNodeBuilder
                .setWindow(mainWindow)
                .setParent(topFocusArea)
                .setBoundsInScreen(new Rect(0, 0, 500, 500))
                .build();
        AccessibilityNodeInfo topRight = mNodeBuilder
                .setWindow(mainWindow)
                .setParent(topFocusArea)
                .setBoundsInScreen(new Rect(500, 0, 1000, 500))
                .build();

        // The bottom focus area has two views that can take focus.
        AccessibilityNodeInfo bottomLeft = mNodeBuilder
                .setWindow(mainWindow)
                .setParent(bottomFocusArea)
                .setBoundsInScreen(new Rect(0, 500, 500, 1000))
                .build();
        AccessibilityNodeInfo bottomRight = mNodeBuilder
                .setWindow(mainWindow)
                .setParent(bottomFocusArea)
                .setBoundsInScreen(new Rect(500, 500, 1000, 1000))
                .build();

        List<AccessibilityWindowInfo> windows = new ArrayList<>();
        windows.add(hunWindow);
        windows.add(mainWindow);

        // Nudging up from the top left or right view should go to the HUN's left button. The
        // source and target overlap so geometric targeting fails. We should fall back to using the
        // first focusable view in the HUN.
        AccessibilityNodeInfo target = mNavigator.findNudgeTarget(windows, topLeft, View.FOCUS_UP);
        assertThat(target).isSameAs(hunLeft);
        target = mNavigator.findNudgeTarget(windows, topRight, View.FOCUS_UP);
        assertThat(target).isSameAs(hunLeft);

        // Nudging up from the bottom left or right view should go to the corresponding button in
        // the HUN, skipping over the top focus area. Geometric targeting should work.
        target = mNavigator.findNudgeTarget(windows, bottomLeft, View.FOCUS_UP);
        assertThat(target).isSameAs(hunLeft);
        target = mNavigator.findNudgeTarget(windows, bottomRight, View.FOCUS_UP);
        assertThat(target).isSameAs(hunRight);
    }

    /**
     * Tests {@link Navigator#findNudgeTarget} in the following layout:
     * <pre>
     * In the same window
     *
     *            ======focus area 1===========
     *            =                  *view1*  =
     *            =============================
     *
     *            ========focus area 2=========
     *            = *view2*                   =
     *            =============================
     *
     *    =====source focus area=====
     *    = *    source view      * =
     *    ===========================
     * </pre>
     */
    @Test
    public void testNudgeToFocusAreaWithNoCandidates() {
        Rect windowBounds = new Rect(0, 0, 600, 500);
        AccessibilityWindowInfo window = new WindowBuilder()
                .setBoundsInScreen(windowBounds)
                .build();
        AccessibilityNodeInfo root = mNodeBuilder
                .setWindow(window)
                .setBoundsInScreen(windowBounds)
                .build();
        setRootNodeForWindow(root, window);

        // Currently focused view.
        AccessibilityNodeInfo sourceFocusArea = mNodeBuilder
                .setWindow(window)
                .setParent(root)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 400, 400, 500))
                .build();
        AccessibilityNodeInfo sourceView = mNodeBuilder
                .setWindow(window)
                .setParent(sourceFocusArea)
                .setBoundsInScreen(new Rect(0, 400, 400, 500))
                .build();

        // focusArea1 is a better candidate than focusArea2 for a nudge to right, but its descendant
        // view is not a candidate.
        AccessibilityNodeInfo focusArea1 = mNodeBuilder
                .setWindow(window)
                .setParent(root)
                .setFocusArea()
                .setBoundsInScreen(new Rect(200, 0, 600, 100))
                .build();
        AccessibilityNodeInfo view1 = mNodeBuilder
                .setWindow(window)
                .setParent(focusArea1)
                .setBoundsInScreen(new Rect(599, 0, 600, 100))
                .build();

        // focusArea2 is a worse candidate than focusArea1 for a nudge to right, but its descendant
        // view is a candidate.
        AccessibilityNodeInfo focusArea2 = mNodeBuilder
                .setWindow(window)
                .setParent(root)
                .setFocusArea()
                .setBoundsInScreen(new Rect(200, 200, 600, 300))
                .build();
        AccessibilityNodeInfo view2 = mNodeBuilder
                .setWindow(window)
                .setParent(focusArea2)
                .setBoundsInScreen(new Rect(200, 200, 201, 300))
                .build();

        List<AccessibilityWindowInfo> windows = new ArrayList<>();
        windows.add(window);

        // Nudge from sourceView to right, and it should go to view1.
        AccessibilityNodeInfo target
                = mNavigator.findNudgeTarget(windows, sourceView, View.FOCUS_RIGHT);
        assertThat(target).isSameAs(view1);
    }

    /**
     * Tests {@link Navigator#findNudgeTarget} in the following layout:
     * <pre>
     * In the same window
     *
     *          =======app bar focus area=========
     *          =             *tab*              =
     *          ==================================
     *          =====browse list focus area ======
     *          =                                =
     *          =                                =
     *          =         *list item 1*          =
     *          =                                =
     *          =                                =
     *          =  ===control bar focus area===  =
     *          =  =   *control bar button*   =  =
     *          =  ============================  =
     *          =                                =
     *          =        *list item 2*           =
     *          ==================================
     * </pre>
     */
    @Test
    public void testNudgeToOverlappedFocusArea() {
        Rect windowBounds = new Rect(0, 0, 100, 100);
        AccessibilityWindowInfo window = new WindowBuilder()
                .setBoundsInScreen(windowBounds)
                .build();
        AccessibilityNodeInfo root = mNodeBuilder
                .setWindow(window)
                .setBoundsInScreen(windowBounds)
                .build();
        setRootNodeForWindow(root, window);

        AccessibilityNodeInfo appBarFocusArea = mNodeBuilder
                .setWindow(window)
                .setParent(root)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 0, 100, 10))
                .build();
        AccessibilityNodeInfo tab = mNodeBuilder
                .setWindow(window)
                .setParent(appBarFocusArea)
                .setBoundsInScreen(new Rect(0, 0, 100, 10))
                .build();

        AccessibilityNodeInfo browseListFocusArea = mNodeBuilder
                .setWindow(window)
                .setParent(root)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 10, 100, 100))
                .build();
        AccessibilityNodeInfo listItem1 = mNodeBuilder
                .setWindow(window)
                .setParent(browseListFocusArea)
                .setBoundsInScreen(new Rect(0, 40, 100, 50))
                .build();
        AccessibilityNodeInfo listItem2 = mNodeBuilder
                .setWindow(window)
                .setParent(browseListFocusArea)
                .setBoundsInScreen(new Rect(0, 90, 100, 100))
                .build();

        AccessibilityNodeInfo ControlBarFocusArea = mNodeBuilder
                .setWindow(window)
                .setParent(root)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 80, 100, 90))
                .build();
        AccessibilityNodeInfo controlButton = mNodeBuilder
                .setWindow(window)
                .setParent(ControlBarFocusArea)
                .setBoundsInScreen(new Rect(0, 80, 100, 90))
                .build();

        List<AccessibilityWindowInfo> windows = new ArrayList<>();
        windows.add(window);

        // Nudge up from controlButton, it should go to listItem1.
        AccessibilityNodeInfo target
                = mNavigator.findNudgeTarget(windows, controlButton, View.FOCUS_UP);
        assertThat(target).isSameAs(listItem1);

        // Nudge up from listItem1, it should go to tab.
        target = mNavigator.findNudgeTarget(windows, listItem1, View.FOCUS_UP);
        assertThat(target).isSameAs(tab);

        // Disable cache.
        mNavigator = new Navigator(
                /* focusHistoryCacheType= */ RotaryCache.CACHE_TYPE_DISABLED,
                /* focusHistoryCacheSize= */ 10,
                /* focusHistoryExpirationTimeMs= */ 0,
                /* focusAreaHistoryCacheType= */ RotaryCache.CACHE_TYPE_DISABLED,
                /* focusAreaHistoryCacheSize= */ 5,
                /* focusAreaHistoryExpirationTimeMs= */ 0,
                /* focusWindowCacheType= */ RotaryCache.CACHE_TYPE_DISABLED,
                /* focusWindowCacheSize= */ 5,
                /* focusWindowExpirationTimeMs= */ 0,
                mHunWindowBounds.left,
                mHunWindowBounds.right,
                /* showHunOnBottom= */ false);
        mNavigator.setNodeCopier(MockNodeCopierProvider.get());

        // Nudge down from listItem1, it should go to controlButton.
        target = mNavigator.findNudgeTarget(windows, listItem1, View.FOCUS_DOWN);
        assertThat(target).isSameAs(controlButton);
    }

    /**
     * Tests {@link Navigator#findNudgeTarget} in the following layout:
     * <pre>
     * In the same window
     *
     *    =====source focus area=====
     *    = *    source view      * =
     *    ===========================
     *
     *    ======target focus area====
     *    =   ---non-focusable----  =
     *    =   -                  -  =
     *    =   -  *target view*   -  =
     *    =   -                  -  =
     *    =   ---view container---  =
     *    ===========================
     * </pre>
     */
    @Test
    public void testNudgeToFocusAreaWithIndirectChild() {
        Rect windowBounds = new Rect(0, 0, 100, 200);
        AccessibilityWindowInfo window = new WindowBuilder()
                .setBoundsInScreen(windowBounds)
                .build();
        AccessibilityNodeInfo root = mNodeBuilder
                .setWindow(window)
                .setBoundsInScreen(windowBounds)
                .build();
        setRootNodeForWindow(root, window);

        // Currently focused view.
        AccessibilityNodeInfo sourceFocusArea = mNodeBuilder
                .setWindow(window)
                .setParent(root)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 0, 100, 100))
                .build();
        AccessibilityNodeInfo sourceView = mNodeBuilder
                .setWindow(window)
                .setParent(sourceFocusArea)
                .setBoundsInScreen(new Rect(0, 0, 100, 100))
                .build();

        // Target view.
        AccessibilityNodeInfo targetFocusArea = mNodeBuilder
                .setWindow(window)
                .setParent(root)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 100, 100, 200))
                .build();
        // viewContainer is non-focusable.
        AccessibilityNodeInfo viewContainer = mNodeBuilder
                .setWindow(window)
                .setParent(targetFocusArea)
                .setFocusable(false)
                .setBoundsInScreen(new Rect(0, 100, 100, 200))
                .build();
        AccessibilityNodeInfo targetView = mNodeBuilder
                .setWindow(window)
                .setParent(viewContainer)
                .setBoundsInScreen(new Rect(0, 100, 100, 200))
                .build();

        List<AccessibilityWindowInfo> windows = new ArrayList<>();
        windows.add(window);

        // Nudge down from sourceView, and it should go to targetView.
        AccessibilityNodeInfo target
                = mNavigator.findNudgeTarget(windows, sourceView, View.FOCUS_DOWN);
        assertThat(target).isSameAs(targetView);
    }

    /**
     * Tests {@link Navigator#findNudgeTarget} in the following layout:
     * <pre>
     * In the same window
     *
     *    =====source focus area=====
     *    = *    source view      * =
     *    ===========================
     *
     *    ======target focus area====
     *    =   -----focusable------  =
     *    =   -                  -  =
     *    =   -  *target view*   -  =
     *    =   -                  -  =
     *    =   ---view container---  =
     *    ===========================
     * </pre>
     */
    @Test
    public void testNudgeToFocusAreaWithNestedFocusableChild() {
        Rect windowBounds = new Rect(0, 0, 100, 200);
        AccessibilityWindowInfo window = new WindowBuilder()
                .setBoundsInScreen(windowBounds)
                .build();
        AccessibilityNodeInfo root = mNodeBuilder
                .setWindow(window)
                .setBoundsInScreen(windowBounds)
                .build();
        setRootNodeForWindow(root, window);

        // Currently focused view.
        AccessibilityNodeInfo sourceFocusArea = mNodeBuilder
                .setWindow(window)
                .setParent(root)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 0, 100, 100))
                .build();
        AccessibilityNodeInfo sourceView = mNodeBuilder
                .setWindow(window)
                .setParent(sourceFocusArea)
                .setBoundsInScreen(new Rect(0, 0, 100, 100))
                .build();

        // Target view.
        AccessibilityNodeInfo targetFocusArea = mNodeBuilder
                .setWindow(window)
                .setParent(root)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 100, 100, 200))
                .build();
        // viewContainer is focusable.
        AccessibilityNodeInfo viewContainer = mNodeBuilder
                .setWindow(window)
                .setParent(targetFocusArea)
                .setBoundsInScreen(new Rect(0, 100, 100, 200))
                .build();
        AccessibilityNodeInfo targetView = mNodeBuilder
                .setWindow(window)
                .setParent(viewContainer)
                .setBoundsInScreen(new Rect(0, 100, 100, 200))
                .build();

        List<AccessibilityWindowInfo> windows = new ArrayList<>();
        windows.add(window);

        // Nudge down from sourceView, and it should go to viewContainer.
        AccessibilityNodeInfo target
                = mNavigator.findNudgeTarget(windows, sourceView, View.FOCUS_DOWN);
        assertThat(target).isSameAs(viewContainer);
    }

    /**
     * Tests {@link Navigator#findNudgeTarget} in the following layout:
     * <pre>
     *    ********* app window **********
     *    *                             *
     *    *  === target focus area ===  *
     *    *  =                       =  *
     *    *  =       [view1]         =  *
     *    *  =                       =  *
     *    *  =    [target view]      =  *
     *    *  =                       =  *
     *    *  =       [view3]         =  *
     *    *  =                       =  *
     *    *  =========================  *
     *    *                             *
     *    *******************************
     *
     *    ********* IME window **********
     *    *                             *
     *    *  === source focus area ===  *
     *    *  =                       =  *
     *    *  =    [source view]      =  *
     *    *  =                       =  *
     *    *  =========================  *
     *    *                             *
     *    *******************************
     * </pre>
     */
    @Test
    public void testNudgeOutOfIme() {
        List<AccessibilityWindowInfo> windows = new ArrayList<>();

        int appWindowId = 0x42;
        Rect appWindowBounds = new Rect(0, 0, 400, 300);
        AccessibilityWindowInfo appWindow = new WindowBuilder()
                .setId(appWindowId)
                .setBoundsInScreen(appWindowBounds)
                .setType(AccessibilityWindowInfo.TYPE_APPLICATION)
                .build();
        windows.add(appWindow);
        AccessibilityNodeInfo appRoot = mNodeBuilder
                .setWindow(appWindow)
                .setWindowId(appWindowId)
                .setBoundsInScreen(appWindowBounds)
                .build();
        setRootNodeForWindow(appRoot, appWindow);
        AccessibilityNodeInfo targetFocusArea = mNodeBuilder
                .setWindow(appWindow)
                .setWindowId(appWindowId)
                .setParent(appRoot)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 0, 400, 300))
                .build();
        AccessibilityNodeInfo view1 = mNodeBuilder
                .setWindow(appWindow)
                .setWindowId(appWindowId)
                .setParent(targetFocusArea)
                .setBoundsInScreen(new Rect(0, 0, 400, 100))
                .build();
        AccessibilityNodeInfo targetView = mNodeBuilder
                .setWindow(appWindow)
                .setWindowId(appWindowId)
                .setParent(targetFocusArea)
                .setBoundsInScreen(new Rect(0, 100, 400, 200))
                .build();
        AccessibilityNodeInfo view3 = mNodeBuilder
                .setWindow(appWindow)
                .setWindowId(appWindowId)
                .setParent(targetFocusArea)
                .setBoundsInScreen(new Rect(0, 200, 400, 300))
                .build();

        int imeWindowId = 0x39;
        Rect imeWindowBounds = new Rect(0, 300, 400, 400);
        AccessibilityWindowInfo imeWindow = new WindowBuilder()
                .setId(imeWindowId)
                .setBoundsInScreen(imeWindowBounds)
                .setType(AccessibilityWindowInfo.TYPE_INPUT_METHOD)
                .build();
        windows.add(imeWindow);
        AccessibilityNodeInfo imeRoot = mNodeBuilder
                .setWindow(imeWindow)
                .setWindowId(imeWindowId)
                .setBoundsInScreen(imeWindowBounds)
                .build();
        setRootNodeForWindow(imeRoot, imeWindow);
        AccessibilityNodeInfo sourceFocusArea = mNodeBuilder
                .setWindow(imeWindow)
                .setWindowId(imeWindowId)
                .setParent(imeRoot)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 300, 400, 400))
                .build();
        AccessibilityNodeInfo sourceView = mNodeBuilder
                .setWindow(imeWindow)
                .setWindowId(imeWindowId)
                .setParent(sourceFocusArea)
                .setBoundsInScreen(new Rect(0, 300, 400, 400))
                .build();

        // Nudge up from sourceView with the targetView already focused, and it should go to
        // targetView. This is what happens when the user nudges up from the IME to go back to the
        // EditText they were editing.
        AccessibilityNodeInfo target
                = mNavigator.findNudgeTarget(windows, sourceView, View.FOCUS_UP, targetView);
        assertThat(target).isSameAs(targetView);
    }

    /**
     * Tests {@link Navigator#findNudgeTarget} in the following layout:
     * <pre>
     *    **********leftWindow*********    ****************rightWindow*****************
     *    *                           *    *                                          *
     *    *  ===left focus area===    *    *    ==========right focus area========    *
     *    *  =                   =    *    *    =                                =    *
     *    *  =                   =    *    *    =  .........scrollable.........  =    *
     *    *  =                   =    *    *    =  .                          .  =    *
     *    *  =      left         =    *    *    =  .      non-focusable       .  =    *
     *    *  =                   =    *    *    =  .                          .  =    *
     *    *  =                   =    *    *    =  .......recyclerView.........  =    *
     *    *  =                   =    *    *    =                                =    *
     *    *  =====================    *    *    ==================================    *
     *    *                           *    *                                          *
     *    *****************************    ********************************************
     * </pre>
     */
    @Test
    public void testFindNudgeTargetReturnScrollableContainer() {
        // There are 2 windows. This is the left window.
        Rect leftWindowBounds = new Rect(0, 0, 400, 400);
        AccessibilityWindowInfo leftWindow = new WindowBuilder()
                .setBoundsInScreen(leftWindowBounds)
                .build();
        AccessibilityNodeInfo leftRoot = mNodeBuilder
                .setWindow(leftWindow)
                .setBoundsInScreen(leftWindowBounds)
                .build();
        setRootNodeForWindow(leftRoot, leftWindow);

        // Left focus area and its view inside.
        AccessibilityNodeInfo leftFocusArea = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(leftRoot)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 0, 400, 400))
                .build();
        AccessibilityNodeInfo left = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(leftFocusArea)
                .setBoundsInScreen(new Rect(0, 0, 400, 400))
                .build();

        // Right window.
        Rect rightWindowBounds = new Rect(400, 0, 800, 400);
        AccessibilityWindowInfo rightWindow = new WindowBuilder()
                .setBoundsInScreen(rightWindowBounds)
                .build();
        AccessibilityNodeInfo rightRoot = mNodeBuilder
                .setWindow(rightWindow)
                .setBoundsInScreen(rightWindowBounds)
                .build();
        setRootNodeForWindow(rightRoot, rightWindow);

        // Right focus area and its view inside.
        AccessibilityNodeInfo rightFocusArea = mNodeBuilder
                .setWindow(rightWindow)
                .setParent(rightRoot)
                .setFocusArea()
                .setBoundsInScreen(new Rect(400, 0, 800, 400))
                .build();
        AccessibilityNodeInfo recyclerView = mNodeBuilder
                .setWindow(rightWindow)
                .setParent(rightFocusArea)
                .setBoundsInScreen(new Rect(400, 0, 800, 400))
                .setScrollableContainer()
                .setScrollable(true)
                .build();
        AccessibilityNodeInfo nonFocusable = mNodeBuilder
                .setWindow(rightWindow)
                .setParent(recyclerView)
                .setFocusable(false)
                .setBoundsInScreen(new Rect(400, 0, 800, 400))
                .build();

        List<AccessibilityWindowInfo> windows = new ArrayList<>();
        windows.add(leftWindow);
        windows.add(rightWindow);

        // Nudge from left window to right window.
        AccessibilityNodeInfo target = mNavigator.findNudgeTarget(windows, left, View.FOCUS_RIGHT);
        assertThat(target).isSameAs(recyclerView);
    }

    /**
     * Tests {@link Navigator#findNudgeTarget} in the following layout:
     * <pre>
     *    **********leftWindow*********    ****************rightWindow*****************
     *    *                           *    *                                          *
     *    *  ===left focus area===    *    *    ==========right focus area========    *
     *    *  =                   =    *    *    =                                =    *
     *    *  =                   =    *    *    =  .........scrollable.........  =    *
     *    *  =                   =    *    *    =  .                          .  =    *
     *    *  =      left         =    *    *    =  .    non-focusable         .  =    *
     *    *  =                   =    *    *    =  .                          .  =    *
     *    *  =                   =    *    *    =  .  focusable(off screen)   .  =    *
     *    *  =                   =    *    *    =  .                          .  =    *
     *    *  =                   =    *    *    =  .......recyclerView.........  =    *
     *    *  =                   =    *    *    =                                =    *
     *    *  =====================    *    *    ==================================    *
     *    *                           *    *                                          *
     *    *****************************    ********************************************
     * </pre>
     */
    @Test
    public void testFindNudgeTargetReturnScrollableContainer2() {
        // There are 2 windows. This is the left window.
        Rect leftWindowBounds = new Rect(0, 0, 400, 400);
        AccessibilityWindowInfo leftWindow = new WindowBuilder()
                .setBoundsInScreen(leftWindowBounds)
                .build();
        AccessibilityNodeInfo leftRoot = mNodeBuilder
                .setWindow(leftWindow)
                .setBoundsInScreen(leftWindowBounds)
                .build();
        setRootNodeForWindow(leftRoot, leftWindow);

        // Left focus area and its view inside.
        AccessibilityNodeInfo leftFocusArea = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(leftRoot)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 0, 400, 400))
                .build();
        AccessibilityNodeInfo left = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(leftFocusArea)
                .setBoundsInScreen(new Rect(0, 0, 400, 400))
                .build();

        // Right window.
        Rect rightWindowBounds = new Rect(400, 0, 800, 400);
        AccessibilityWindowInfo rightWindow = new WindowBuilder()
                .setBoundsInScreen(rightWindowBounds)
                .build();
        AccessibilityNodeInfo rightRoot = mNodeBuilder
                .setWindow(rightWindow)
                .setBoundsInScreen(rightWindowBounds)
                .build();
        setRootNodeForWindow(rightRoot, rightWindow);

        // Right focus area and its view inside.
        AccessibilityNodeInfo rightFocusArea = mNodeBuilder
                .setWindow(rightWindow)
                .setParent(rightRoot)
                .setFocusArea()
                .setBoundsInScreen(new Rect(400, 0, 800, 400))
                .build();
        AccessibilityNodeInfo recyclerView = mNodeBuilder
                .setWindow(rightWindow)
                .setParent(rightFocusArea)
                .setBoundsInScreen(new Rect(400, 0, 800, 400))
                .setScrollableContainer()
                .setScrollable(true)
                .build();
        AccessibilityNodeInfo nonFocusable = mNodeBuilder
                .setWindow(rightWindow)
                .setParent(recyclerView)
                .setFocusable(false)
                .setBoundsInScreen(new Rect(400, 0, 800, 400))
                .build();
        AccessibilityNodeInfo focusable = mNodeBuilder
                .setWindow(rightWindow)
                .setParent(recyclerView)
                .setBoundsInScreen(new Rect(0, 0, 0, 0))
                .build();

        List<AccessibilityWindowInfo> windows = new ArrayList<>();
        windows.add(leftWindow);
        windows.add(rightWindow);

        // Nudge from left window to right window.
        AccessibilityNodeInfo target = mNavigator.findNudgeTarget(windows, left, View.FOCUS_RIGHT);
        assertThat(target).isSameAs(recyclerView);
    }

    /**
     * Tests {@link Navigator#findNudgeTarget} in the following layout:
     * <pre>
     *    **********leftWindow*********    ****************rightWindow*****************
     *    *                           *    *                                          *
     *    *  ===left focus area===    *    *    ==========right focus area========    *
     *    *  =                   =    *    *    =                                =    *
     *    *  =                   =    *    *    =  .......non-scrollable.......  =    *
     *    *  =                   =    *    *    =  .                          .  =    *
     *    *  =      left         =    *    *    =  .    non-focusable         .  =    *
     *    *  =                   =    *    *    =  .                          .  =    *
     *    *  =                   =    *    *    =  .......recyclerView.........  =    *
     *    *  =                   =    *    *    =                                =    *
     *    *  =====================    *    *    ==================================    *
     *    *                           *    *                                          *
     *    *****************************    ********************************************
     * </pre>
     */
    @Test
    public void testFindNudgeTargetSkipScrollableContainer() {
        // There are 2 windows. This is the left window.
        Rect leftWindowBounds = new Rect(0, 0, 400, 400);
        AccessibilityWindowInfo leftWindow = new WindowBuilder()
                .setBoundsInScreen(leftWindowBounds)
                .build();
        AccessibilityNodeInfo leftRoot = mNodeBuilder
                .setWindow(leftWindow)
                .setBoundsInScreen(leftWindowBounds)
                .build();
        setRootNodeForWindow(leftRoot, leftWindow);

        // Left focus area and its view inside.
        AccessibilityNodeInfo leftFocusArea = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(leftRoot)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 0, 400, 400))
                .build();
        AccessibilityNodeInfo left = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(leftFocusArea)
                .setBoundsInScreen(new Rect(0, 0, 400, 400))
                .build();

        // Right window.
        Rect rightWindowBounds = new Rect(400, 0, 800, 400);
        AccessibilityWindowInfo rightWindow = new WindowBuilder()
                .setBoundsInScreen(rightWindowBounds)
                .build();
        AccessibilityNodeInfo rightRoot = mNodeBuilder
                .setWindow(rightWindow)
                .setBoundsInScreen(rightWindowBounds)
                .build();
        setRootNodeForWindow(rightRoot, rightWindow);

        // Right focus area and its view inside.
        AccessibilityNodeInfo rightFocusArea = mNodeBuilder
                .setWindow(rightWindow)
                .setParent(rightRoot)
                .setFocusArea()
                .setBoundsInScreen(new Rect(400, 0, 800, 400))
                .build();
        AccessibilityNodeInfo recyclerView = mNodeBuilder
                .setWindow(rightWindow)
                .setParent(rightFocusArea)
                .setBoundsInScreen(new Rect(400, 0, 800, 400))
                .setScrollableContainer()
                .build();
        AccessibilityNodeInfo nonFocusable = mNodeBuilder
                .setWindow(rightWindow)
                .setParent(recyclerView)
                .setFocusable(false)
                .setBoundsInScreen(new Rect(400, 0, 800, 400))
                .build();

        List<AccessibilityWindowInfo> windows = new ArrayList<>();
        windows.add(leftWindow);
        windows.add(rightWindow);

        // Nudge from left window to right window.
        AccessibilityNodeInfo target = mNavigator.findNudgeTarget(windows, left, View.FOCUS_RIGHT);
        assertThat(target).isSameAs(null);
    }


    /**
     * Tests {@link Navigator#findNudgeTarget} in the following layout:
     * <pre>
     *    **********leftWindow*********    ****************rightWindow*****************
     *    *                           *    *                                          *
     *    *  ===left focus area===    *    *    ==========right focus area========    *
     *    *  =                   =    *    *    =                                =    *
     *    *  =                   =    *    *    =  .........scrollable.........  =    *
     *    *  =                   =    *    *    =  .                          .  =    *
     *    *  =      left         =    *    *    =  .       focusable          .  =    *
     *    *  =                   =    *    *    =  .                          .  =    *
     *    *  =                   =    *    *    =  .......recyclerView.........  =    *
     *    *  =                   =    *    *    =                                =    *
     *    *  =====================    *    *    ==================================    *
     *    *                           *    *                                          *
     *    *****************************    ********************************************
     * </pre>
     */
    @Test
    public void testFindNudgeTargetSkipScrollableContainer2() {
        // There are 2 windows. This is the left window.
        Rect leftWindowBounds = new Rect(0, 0, 400, 400);
        AccessibilityWindowInfo leftWindow = new WindowBuilder()
                .setBoundsInScreen(leftWindowBounds)
                .build();
        AccessibilityNodeInfo leftRoot = mNodeBuilder
                .setWindow(leftWindow)
                .setBoundsInScreen(leftWindowBounds)
                .build();
        setRootNodeForWindow(leftRoot, leftWindow);

        // Left focus area and its view inside.
        AccessibilityNodeInfo leftFocusArea = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(leftRoot)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 0, 400, 400))
                .build();
        AccessibilityNodeInfo left = mNodeBuilder
                .setWindow(leftWindow)
                .setParent(leftFocusArea)
                .setBoundsInScreen(new Rect(0, 0, 400, 400))
                .build();

        // Right window.
        Rect rightWindowBounds = new Rect(400, 0, 800, 400);
        AccessibilityWindowInfo rightWindow = new WindowBuilder()
                .setBoundsInScreen(rightWindowBounds)
                .build();
        AccessibilityNodeInfo rightRoot = mNodeBuilder
                .setWindow(rightWindow)
                .setBoundsInScreen(rightWindowBounds)
                .build();
        setRootNodeForWindow(rightRoot, rightWindow);

        // Right focus area and its view inside.
        AccessibilityNodeInfo rightFocusArea = mNodeBuilder
                .setWindow(rightWindow)
                .setParent(rightRoot)
                .setFocusArea()
                .setBoundsInScreen(new Rect(400, 0, 800, 400))
                .build();
        AccessibilityNodeInfo recyclerView = mNodeBuilder
                .setWindow(rightWindow)
                .setParent(rightFocusArea)
                .setBoundsInScreen(new Rect(400, 0, 800, 400))
                .setScrollableContainer()
                .setScrollable(true)
                .build();
        AccessibilityNodeInfo focusable = mNodeBuilder
                .setWindow(rightWindow)
                .setParent(recyclerView)
                .setBoundsInScreen(new Rect(400, 0, 800, 400))
                .build();

        List<AccessibilityWindowInfo> windows = new ArrayList<>();
        windows.add(leftWindow);
        windows.add(rightWindow);

        // Nudge from left window to right window.
        AccessibilityNodeInfo target = mNavigator.findNudgeTarget(windows, left, View.FOCUS_RIGHT);
        assertThat(target).isSameAs(focusable);
    }

    /**
     * Tests {@link Navigator#findNudgeTarget} in the following layout:
     * <pre>
     * In the same window
     *
     *          =====contact list focus area======        ========app bar focus area========
     *          = ********contact list********** =  <-->  =            *tab*               =
     *          = *                            * =        ==================================
     *          = *                            * =
     *          = *                            * =
     *          = *   non-focusable item1      * =
     *          = *                            * =
     *          = *   non-focusable item2      * =
     *          = *                            * =
     *          = ********recyclerView********** =
     *          ==================================
     *
     *          ========nav bar focus area========
     *          =            *button*            =
     *          ==================================
     * </pre>
     * Where app bar focus area overlaps with contact list focus area.
     */
    @Test
    public void testFindNudgeTargetReturnContactList() {
        Rect windowBounds = new Rect(0, 0, 100, 100);
        AccessibilityWindowInfo window = new WindowBuilder()
                .setBoundsInScreen(windowBounds)
                .build();
        AccessibilityNodeInfo root = mNodeBuilder
                .setWindow(window)
                .setBoundsInScreen(windowBounds)
                .build();
        setRootNodeForWindow(root, window);

        AccessibilityNodeInfo contactListFocusArea = mNodeBuilder
                .setWindow(window)
                .setParent(root)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 0, 100, 80))
                .build();
        AccessibilityNodeInfo contactList = mNodeBuilder
                .setWindow(window)
                .setParent(contactListFocusArea)
                .setBoundsInScreen(new Rect(0, 0, 100, 80))
                .setScrollableContainer()
                .setScrollable(true)
                .build();

        AccessibilityNodeInfo item1 = mNodeBuilder
                .setWindow(window)
                .setParent(contactList)
                .setFocusable(false)
                .setBoundsInScreen(new Rect(0, 40, 100, 50))
                .build();
        AccessibilityNodeInfo item2 = mNodeBuilder
                .setWindow(window)
                .setParent(contactList)
                .setFocusable(false)
                .setBoundsInScreen(new Rect(0, 50, 100, 60))
                .build();

        AccessibilityNodeInfo appBarFocusArea = mNodeBuilder
                .setWindow(window)
                .setParent(root)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 0, 100, 20))
                .build();
        AccessibilityNodeInfo tab = mNodeBuilder
                .setWindow(window)
                .setParent(appBarFocusArea)
                .setBoundsInScreen(new Rect(40, 0, 50, 20))
                .build();

        AccessibilityNodeInfo navBarFocusArea = mNodeBuilder
                .setWindow(window)
                .setParent(root)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 80, 100, 100))
                .build();
        AccessibilityNodeInfo button = mNodeBuilder
                .setWindow(window)
                .setParent(navBarFocusArea)
                .setBoundsInScreen(new Rect(40, 80, 50, 100))
                .build();

        List<AccessibilityWindowInfo> windows = new ArrayList<>();
        windows.add(window);

        // Nudge down from tab, it should go to contact list.
        AccessibilityNodeInfo target
                = mNavigator.findNudgeTarget(windows, tab, View.FOCUS_DOWN);
        assertThat(target).isSameAs(contactList);
    }

    /**
     * Tests {@link Navigator#findNudgeTarget} in the following layout:
     * <pre>
     * In the same window
     *
     *          ==========focus area 1============
     *          =  .........top offset.........  =
     *          =  .        *view1*           .  =
     *          =  .......bottom offset........  =
     *          =                                =
     *          =  ========focus area 2========  =
     *          =  =         *view2*          =  =
     *          =  ============================  =
     *          =                                =
     *          =                                =
     *          =                                =
     *          =                                =
     *          ==================================
     *
     *          ===========focus area 3===========
     *          =            *view3*             =
     *          ==================================
     * </pre>
     */
    @Test
    public void testFindNudgeTargetWithFocusAreaBoundsOffset() {
        Rect windowBounds = new Rect(0, 0, 100, 100);
        AccessibilityWindowInfo window = new WindowBuilder()
                .setBoundsInScreen(windowBounds)
                .build();
        AccessibilityNodeInfo root = mNodeBuilder
                .setWindow(window)
                .setBoundsInScreen(windowBounds)
                .build();
        setRootNodeForWindow(root, window);

        AccessibilityNodeInfo focusArea1 = mNodeBuilder
                .setWindow(window)
                .setParent(root)
                .setFocusArea()
                .setFocusAreaBoundsOffset(0, 0, 0, 70)
                .setBoundsInScreen(new Rect(0, 0, 100, 80))
                .build();
        AccessibilityNodeInfo view1 = mNodeBuilder
                .setWindow(window)
                .setParent(focusArea1)
                .setBoundsInScreen(new Rect(0, 0, 100, 10))
                .build();

        AccessibilityNodeInfo focusArea2 = mNodeBuilder
                .setWindow(window)
                .setParent(root)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 10, 100, 20))
                .build();
        AccessibilityNodeInfo view2 = mNodeBuilder
                .setWindow(window)
                .setParent(focusArea2)
                .setBoundsInScreen(new Rect(0, 10, 100, 20))
                .build();

        AccessibilityNodeInfo focusArea3 = mNodeBuilder
                .setWindow(window)
                .setParent(root)
                .setFocusArea()
                .setBoundsInScreen(new Rect(0, 90, 100, 100))
                .build();
        AccessibilityNodeInfo view3 = mNodeBuilder
                .setWindow(window)
                .setParent(focusArea3)
                .setBoundsInScreen(new Rect(0, 90, 100, 100))
                .build();

        List<AccessibilityWindowInfo> windows = new ArrayList<>();
        windows.add(window);

        // Nudge up from view3, it should go to view2.
        AccessibilityNodeInfo target
                = mNavigator.findNudgeTarget(windows, view3, View.FOCUS_UP);
        assertThat(target).isSameAs(view2);
    }

    /**
     * Tests {@link Navigator#findFirstFocusDescendant} in the following node tree:
     * <pre>
     *                   root
     *                  /    \
     *                /       \
     *          focusArea1  focusArea2
     *           /   \          /   \
     *         /      \        /     \
     *     button1 button2 button3 button4
     * </pre>
     */
    @Test
    public void testFindFirstFocusDescendant() {
        AccessibilityNodeInfo root = mNodeBuilder.setFocusable(false).build();
        AccessibilityNodeInfo focusArea1 = mNodeBuilder.setParent(root).setFocusArea().build();
        AccessibilityNodeInfo focusArea2 = mNodeBuilder.setParent(root).setFocusArea().build();

        AccessibilityNodeInfo button1 = mNodeBuilder.setParent(focusArea1).build();
        AccessibilityNodeInfo button2 = mNodeBuilder.setParent(focusArea1).build();
        AccessibilityNodeInfo button3 = mNodeBuilder.setParent(focusArea2).build();
        AccessibilityNodeInfo button4 = mNodeBuilder.setParent(focusArea2).build();

        int direction = View.FOCUS_FORWARD;

        // Search forward from the focus area.
        when(focusArea1.focusSearch(direction)).thenReturn(button2);
        AccessibilityNodeInfo target = mNavigator.findFirstFocusDescendant(root);
        assertThat(target).isSameAs(button2);

        // Fall back to tree traversal.
        when(focusArea1.focusSearch(direction)).thenReturn(null);
        target = mNavigator.findFirstFocusDescendant(root);
        assertThat(target).isSameAs(button1);
    }

    /**
     * Tests {@link Navigator#findFirstFocusDescendant} in the following node tree:
     * <pre>
     *                     root
     *                    /    \
     *                   /      \
     *      focusParkingView   focusArea
     *                           /    \
     *                          /      \
     *                      button1   button2
     * </pre>
     */
    @Test
    public void testFindFirstFocusDescendantWithFocusParkingView() {
        AccessibilityNodeInfo root = mNodeBuilder.setFocusable(false).build();
        AccessibilityNodeInfo focusParkingView = mNodeBuilder.setParent(root).setFpv().build();
        AccessibilityNodeInfo focusArea = mNodeBuilder.setParent(root).setFocusArea().build();

        AccessibilityNodeInfo button1 = mNodeBuilder.setParent(focusArea).build();
        AccessibilityNodeInfo button2 = mNodeBuilder.setParent(focusArea).build();

        int direction = View.FOCUS_FORWARD;

        // Search forward from the focus area.
        when(focusArea.focusSearch(direction)).thenReturn(button2);
        AccessibilityNodeInfo target = mNavigator.findFirstFocusDescendant(root);
        assertThat(target).isSameAs(button2);

        // Fall back to tree traversal.
        when(focusArea.focusSearch(direction)).thenReturn(null);
        target = mNavigator.findFirstFocusDescendant(root);
        assertThat(target).isSameAs(button1);
    }

    /**
     * Tests {@link Navigator#findScrollableContainer} in the following node tree:
     * <pre>
     *                root
     *                 |
     *                 |
     *             focusArea
     *              /     \
     *            /         \
     *        scrolling    button2
     *        container
     *           |
     *           |
     *       container
     *           |
     *           |
     *        button1
     * </pre>
     */
    @Test
    public void testFindScrollableContainer() {
        AccessibilityNodeInfo root = mNodeBuilder.build();
        AccessibilityNodeInfo focusArea = mNodeBuilder.setParent(root).setFocusArea().build();
        AccessibilityNodeInfo scrollableContainer = mNodeBuilder
                .setParent(focusArea)
                .setScrollableContainer()
                .build();
        AccessibilityNodeInfo container = mNodeBuilder.setParent(scrollableContainer).build();
        AccessibilityNodeInfo button1 = mNodeBuilder.setParent(container).build();
        AccessibilityNodeInfo button2 = mNodeBuilder.setParent(focusArea).build();

        AccessibilityNodeInfo target = mNavigator.findScrollableContainer(button1);
        assertThat(target).isSameAs(scrollableContainer);
        target = mNavigator.findScrollableContainer(button2);
        assertThat(target).isNull();
    }

    /**
     * Tests {@link Navigator#findFocusableDescendantInDirection} going
     *      * {@link View#FOCUS_BACKWARD} in the following node tree:
     * <pre>
     *                     root
     *                   /      \
     *                 /          \
     *         container1        container2
     *           /   \             /   \
     *         /       \         /       \
     *     button1   button2  button3  button4
     * </pre>
     */
    @Test
    public void testFindFocusableVisibleDescendantInDirectionBackward() {
        AccessibilityNodeInfo root = mNodeBuilder.build();
        AccessibilityNodeInfo container1 = mNodeBuilder.setParent(root).build();
        AccessibilityNodeInfo button1 = mNodeBuilder.setParent(container1).build();
        AccessibilityNodeInfo button2 = mNodeBuilder.setParent(container1).build();
        AccessibilityNodeInfo container2 = mNodeBuilder.setParent(root).build();
        AccessibilityNodeInfo button3 = mNodeBuilder.setParent(container2).build();
        AccessibilityNodeInfo button4 = mNodeBuilder.setParent(container2).build();

        int direction = View.FOCUS_BACKWARD;
        when(button4.focusSearch(direction)).thenReturn(button3);
        when(button3.focusSearch(direction)).thenReturn(button2);
        when(button2.focusSearch(direction)).thenReturn(button1);
        when(button1.focusSearch(direction)).thenReturn(null);

        AccessibilityNodeInfo target = mNavigator.findFocusableDescendantInDirection(
                container2, button4, View.FOCUS_BACKWARD);
        assertThat(target).isSameAs(button3);
        target = mNavigator.findFocusableDescendantInDirection(container2, button3,
                View.FOCUS_BACKWARD);
        assertThat(target).isNull();
        target = mNavigator.findFocusableDescendantInDirection(container1, button2,
                View.FOCUS_BACKWARD);
        assertThat(target).isSameAs(button1);
        target = mNavigator.findFocusableDescendantInDirection(container1, button1,
                View.FOCUS_BACKWARD);
        assertThat(target).isNull();
    }

    /**
     * Tests {@link Navigator#findFocusableDescendantInDirection} going
     * {@link View#FOCUS_FORWARD} in the following node tree:
     * <pre>
     *                     root
     *                   /      \
     *                 /          \
     *         container1        container2
     *           /   \             /   \
     *         /       \         /       \
     *     button1   button2  button3  button4
     * </pre>
     */
    @Test
    public void testFindFocusableVisibleDescendantInDirectionForward() {
        AccessibilityNodeInfo root = mNodeBuilder.build();
        AccessibilityNodeInfo container1 = mNodeBuilder.setParent(root).build();
        AccessibilityNodeInfo button1 = mNodeBuilder.setParent(container1).build();
        AccessibilityNodeInfo button2 = mNodeBuilder.setParent(container1).build();
        AccessibilityNodeInfo container2 = mNodeBuilder.setParent(root).build();
        AccessibilityNodeInfo button3 = mNodeBuilder.setParent(container2).build();
        AccessibilityNodeInfo button4 = mNodeBuilder.setParent(container2).build();

        int direction = View.FOCUS_FORWARD;
        when(button1.focusSearch(direction)).thenReturn(button2);
        when(button2.focusSearch(direction)).thenReturn(button3);
        when(button3.focusSearch(direction)).thenReturn(button4);
        when(button4.focusSearch(direction)).thenReturn(null);

        AccessibilityNodeInfo target = mNavigator.findFocusableDescendantInDirection(
                container1, button1, View.FOCUS_FORWARD);
        assertThat(target).isSameAs(button2);
        target = mNavigator.findFocusableDescendantInDirection(container1, button2,
                View.FOCUS_FORWARD);
        assertThat(target).isNull();
        target = mNavigator.findFocusableDescendantInDirection(container2, button3,
                View.FOCUS_FORWARD);
        assertThat(target).isSameAs(button4);
        target = mNavigator.findFocusableDescendantInDirection(container2, button4,
                View.FOCUS_FORWARD);
        assertThat(target).isNull();
    }

    /**
     * Tests {@link Navigator#findNextFocusableDescendant} in the following node tree:
     * <pre>
     *                     root
     *                      |
     *                      |
     *                  container
     *               /    /   \   \
     *            /      /     \      \
     *     button1  button2  button3  button4
     * </pre>
     * where {@code button3} and {@code button4} have empty bounds.
     */
    @Test
    public void testFindNextFocusableDescendantWithEmptyBounds() {
        AccessibilityNodeInfo root = mNodeBuilder.build();
        AccessibilityNodeInfo container = mNodeBuilder.setParent(root).build();
        AccessibilityNodeInfo button1 = mNodeBuilder.setParent(container).build();
        AccessibilityNodeInfo button2 = mNodeBuilder.setParent(container).build();
        AccessibilityNodeInfo button3 = mNodeBuilder.setParent(container)
                .setBoundsInScreen(new Rect(5, 10, 5, 10)).build();
        AccessibilityNodeInfo button4 = mNodeBuilder.setParent(container)
                .setBoundsInScreen(new Rect(20, 40, 20, 40)).build();

        int direction = View.FOCUS_FORWARD;
        when(button1.focusSearch(direction)).thenReturn(button2);
        when(button2.focusSearch(direction)).thenReturn(button3);
        when(button3.focusSearch(direction)).thenReturn(button4);
        when(button4.focusSearch(direction)).thenReturn(button1);

        AccessibilityNodeInfo target = mNavigator.findFocusableDescendantInDirection(container,
                button1, View.FOCUS_FORWARD);
        assertThat(target).isSameAs(button2);
        target = mNavigator.findFocusableDescendantInDirection(container, button2,
                View.FOCUS_FORWARD);
        assertThat(target).isSameAs(button1);
        target = mNavigator.findFocusableDescendantInDirection(container, button3,
                View.FOCUS_FORWARD);
        assertThat(target).isSameAs(button1);
        target = mNavigator.findFocusableDescendantInDirection(container, button4,
                View.FOCUS_FORWARD);
        assertThat(target).isSameAs(button1);
    }

    /**
     * Tests {@link Navigator#findFirstFocusableDescendant} in the following node tree:
     * <pre>
     *                     root
     *                   /      \
     *                 /          \
     *         container1        container2
     *           /   \             /   \
     *         /       \         /       \
     *     button1   button2  button3  button4
     * </pre>
     * where {@code button1} and {@code button2} are disabled.
     */
    @Test
    public void testFindFirstFocusableDescendant() {
        AccessibilityNodeInfo root = mNodeBuilder.setFocusable(false).build();
        AccessibilityNodeInfo container1 = mNodeBuilder
                .setParent(root)
                .setFocusable(false)
                .build();
        AccessibilityNodeInfo button1 = mNodeBuilder
                .setParent(container1)
                .setEnabled(false)
                .build();
        AccessibilityNodeInfo button2 = mNodeBuilder
                .setParent(container1)
                .setEnabled(false)
                .build();
        AccessibilityNodeInfo container2 = mNodeBuilder
                .setParent(root)
                .setFocusable(false)
                .build();
        AccessibilityNodeInfo button3 = mNodeBuilder.setParent(container2).build();
        AccessibilityNodeInfo button4 = mNodeBuilder.setParent(container2).build();

        AccessibilityNodeInfo target = mNavigator.findFirstFocusableDescendant(root);
        assertThat(target).isSameAs(button3);
    }

    /**
     * Tests {@link Navigator#findLastFocusableDescendant} in the following node tree:
     * <pre>
     *                     root
     *                   /      \
     *                 /          \
     *         container1        container2
     *           /   \             /   \
     *         /       \         /       \
     *     button1   button2  button3  button4
     * </pre>
     * where {@code button3} and {@code button4} are disabled.
     */
    @Test
    public void testFindLastFocusableDescendant() {
        AccessibilityNodeInfo root = mNodeBuilder.setFocusable(false).build();
        AccessibilityNodeInfo container1 = mNodeBuilder
                .setParent(root)
                .setFocusable(false)
                .build();
        AccessibilityNodeInfo button1 = mNodeBuilder.setParent(container1).build();
        AccessibilityNodeInfo button2 = mNodeBuilder.setParent(container1).build();
        AccessibilityNodeInfo container2 = mNodeBuilder
                .setParent(root)
                .setFocusable(false)
                .build();
        AccessibilityNodeInfo button3 = mNodeBuilder
                .setParent(container2)
                .setEnabled(false)
                .build();
        AccessibilityNodeInfo button4 = mNodeBuilder
                .setParent(container2)
                .setEnabled(false)
                .build();

        AccessibilityNodeInfo target = mNavigator.findLastFocusableDescendant(root);
        assertThat(target).isSameAs(button2);
    }

    /** Sets the {@code root} node in the {@code window}'s hierarchy. */
    private void setRootNodeForWindow(@NonNull AccessibilityNodeInfo root,
            @NonNull AccessibilityWindowInfo window) {
        when(window.getRoot()).thenReturn(root);
    }
}
