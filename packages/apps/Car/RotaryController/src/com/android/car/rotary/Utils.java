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

import static com.android.car.ui.utils.RotaryConstants.FOCUS_AREA_BOTTOM_BOUND_OFFSET;
import static com.android.car.ui.utils.RotaryConstants.FOCUS_AREA_LEFT_BOUND_OFFSET;
import static com.android.car.ui.utils.RotaryConstants.FOCUS_AREA_RIGHT_BOUND_OFFSET;
import static com.android.car.ui.utils.RotaryConstants.FOCUS_AREA_TOP_BOUND_OFFSET;
import static com.android.car.ui.utils.RotaryConstants.ROTARY_HORIZONTALLY_SCROLLABLE;
import static com.android.car.ui.utils.RotaryConstants.ROTARY_VERTICALLY_SCROLLABLE;

import android.graphics.Rect;
import android.os.Bundle;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityWindowInfo;
import android.webkit.WebView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import com.android.car.ui.FocusArea;
import com.android.car.ui.FocusParkingView;

import java.util.List;

/**
 * Utility methods for {@link AccessibilityNodeInfo} and {@link AccessibilityWindowInfo}.
 * <p>
 * Because {@link AccessibilityNodeInfo}s must be recycled, it's important to be consistent about
 * who is responsible for recycling them. For simplicity, it's best to avoid having multiple objects
 * refer to the same instance of {@link AccessibilityNodeInfo}. Instead, each object should keep its
 * own copy which it's responsible for. Methods that return an {@link AccessibilityNodeInfo}
 * generally pass ownership to the caller. Such methods should never return a reference to one of
 * their parameters or the caller will recycle it twice.
 */
final class Utils {

    @VisibleForTesting
    static final String FOCUS_AREA_CLASS_NAME = FocusArea.class.getName();
    @VisibleForTesting
    static final String FOCUS_PARKING_VIEW_CLASS_NAME = FocusParkingView.class.getName();
    private static final String WEB_VIEW_CLASS_NAME = WebView.class.getName();

    private Utils() {
    }

    /** Recycles a node. */
    static void recycleNode(@Nullable AccessibilityNodeInfo node) {
        if (node != null) {
            node.recycle();
        }
    }

    /** Recycles a list of nodes. */
    static void recycleNodes(@Nullable List<AccessibilityNodeInfo> nodes) {
        if (nodes != null) {
            for (AccessibilityNodeInfo node : nodes) {
                recycleNode(node);
            }
        }
    }

    /**
     * Updates the given {@code node} in case the view represented by it is no longer in the view
     * tree. If it's still in the view tree, returns the {@code node}. Otherwise recycles the
     * {@code node} and returns null.
     */
    static AccessibilityNodeInfo refreshNode(@Nullable AccessibilityNodeInfo node) {
        if (node == null) {
            return null;
        }
        boolean succeeded = node.refresh();
        if (succeeded) {
            return node;
        }
        L.w("This node is no longer in the view tree: " + node);
        node.recycle();
        return null;
    }

    /**
     * Returns whether RotaryService can call {@code performFocusAction()} with the given
     * {@code node}.
     * <p>
     * We don't check if the node is visible because we want to allow nodes scrolled off the screen
     * to be focused.
     */
    static boolean canPerformFocus(@NonNull AccessibilityNodeInfo node) {
        if (!node.isFocusable() || !node.isEnabled()) {
            return false;
        }

        // ACTION_FOCUS doesn't work on WebViews.
        if (isWebView(node)) {
            return false;
        }

        // Check the bounds in the parent rather than the bounds in the screen because the latter
        // are always empty for views that are off screen.
        Rect bounds = new Rect();
        node.getBoundsInParent(bounds);
        return !bounds.isEmpty();
    }

    /**
     * Returns whether the given {@code node} can be focused by the rotary controller.
     * <ul>
     *     <li>To be a focus candidate, a node must be able to perform focus action.
     *     <li>A {@link FocusParkingView} is not a focus candidate.
     *     <li>A scrollable container is not a focus candidate unless it should scroll (i.e.,
     *         is scrollable and has no focusable descendants on screen). We skip a container
     *         because we want to focus on its element directly. We don't skip a container because
     *         we want to focus on it, thus we can scroll it when the rotary controller is rotated.
     *     <li>To be a focus candidate, a node must be on the screen. Usually the node off the
     *         screen (its bounds in screen is empty) is ignored by RotaryService, but there are
     *         exceptions, e.g. nodes in a WebView.
     * </ul>
     */
    static boolean canTakeFocus(@NonNull AccessibilityNodeInfo node) {
        boolean result = canPerformFocus(node)
                && !isFocusParkingView(node)
                && (!isScrollableContainer(node)
                        || (node.isScrollable() && !descendantCanTakeFocus(node)));
        if (result) {
            Rect bounds = getBoundsInScreen(node);
            if (!bounds.isEmpty()) {
                return true;
            }
            L.d("node is off the screen but it's not ignored by RotaryService: " + node);
        }
        return false;
    }

    /** Returns whether the given {@code node} or its descendants can take focus. */
    static boolean canHaveFocus(@NonNull AccessibilityNodeInfo node) {
        return canTakeFocus(node) || descendantCanTakeFocus(node);
    }

    /** Returns whether the given {@code node}'s descendants can take focus. */
    static boolean descendantCanTakeFocus(@NonNull AccessibilityNodeInfo node) {
        for (int i = 0; i < node.getChildCount(); i++) {
            AccessibilityNodeInfo childNode = node.getChild(i);
            if (childNode != null) {
                boolean result = canHaveFocus(childNode);
                childNode.recycle();
                if (result) {
                    return true;
                }
            }
        }
        return false;
    }

    /**
     * Returns whether the given {@code node} has focus (i.e. the node or one of its descendants is
     * focused).
     */
    static boolean hasFocus(@NonNull AccessibilityNodeInfo node) {
        if (node.isFocused()) {
            return true;
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            AccessibilityNodeInfo childNode = node.getChild(i);
            if (childNode != null) {
                boolean result = hasFocus(childNode);
                childNode.recycle();
                if (result) {
                    return true;
                }
            }
        }
        return false;
    }

    /** Returns whether the given {@code node} represents a {@link FocusParkingView}. */
    static boolean isFocusParkingView(@NonNull AccessibilityNodeInfo node) {
        CharSequence className = node.getClassName();
        return className != null && FOCUS_PARKING_VIEW_CLASS_NAME.contentEquals(className);
    }

    /** Returns whether the given {@code node} represents a {@link FocusArea}. */
    static boolean isFocusArea(@NonNull AccessibilityNodeInfo node) {
        CharSequence className = node.getClassName();
        return className != null && FOCUS_AREA_CLASS_NAME.contentEquals(className);
    }

    /**
     * Returns whether {@code node} represents a {@code WebView} or the root of the document within
     * one.
     * <p>
     * The descendants of a node representing a {@code WebView} represent HTML elements rather
     * than {@code View}s so {@link AccessibilityNodeInfo#focusSearch} doesn't work for these nodes.
     * The focused state of these nodes isn't reliable. The node representing a {@code WebView} has
     * a single child node representing the HTML document. This node also claims to be a {@code
     * WebView}. Unlike its parent, it is scrollable and focusable.
     */
    static boolean isWebView(@NonNull AccessibilityNodeInfo node) {
        CharSequence className = node.getClassName();
        return className != null && WEB_VIEW_CLASS_NAME.contentEquals(className);
    }

    /**
     * Returns whether the given node represents a view which can be scrolled using the rotary
     * controller, as indicated by its content description.
     */
    static boolean isScrollableContainer(@NonNull AccessibilityNodeInfo node) {
        CharSequence contentDescription = node.getContentDescription();
        return contentDescription != null
                && (ROTARY_HORIZONTALLY_SCROLLABLE.contentEquals(contentDescription)
                || ROTARY_VERTICALLY_SCROLLABLE.contentEquals(contentDescription));
    }

    /**
     * Returns whether the given node represents a view which can be scrolled horizontally using the
     * rotary controller, as indicated by its content description.
     */
    static boolean isHorizontallyScrollableContainer(@NonNull AccessibilityNodeInfo node) {
        CharSequence contentDescription = node.getContentDescription();
        return contentDescription != null
                && (ROTARY_HORIZONTALLY_SCROLLABLE.contentEquals(contentDescription));
    }

    /** Returns whether {@code descendant} is a descendant of {@code ancestor}. */
    static boolean isDescendant(@NonNull AccessibilityNodeInfo ancestor,
            @NonNull AccessibilityNodeInfo descendant) {
        AccessibilityNodeInfo parent = descendant.getParent();
        if (parent == null) {
            return false;
        }
        boolean result = parent.equals(ancestor) || isDescendant(ancestor, parent);
        recycleNode(parent);
        return result;
    }

    /** Recycles a window. */
    static void recycleWindow(@Nullable AccessibilityWindowInfo window) {
        if (window != null) {
            window.recycle();
        }
    }

    /** Recycles a list of windows. */
    static void recycleWindows(@Nullable List<AccessibilityWindowInfo> windows) {
        if (windows != null) {
            for (AccessibilityWindowInfo window : windows) {
                recycleWindow(window);
            }
        }
    }

    /**
     * Returns a reference to the window with ID {@code windowId} or null if not found.
     * <p>
     * <strong>Note:</strong> Do not recycle the result.
     */
    @Nullable
    static AccessibilityWindowInfo findWindowWithId(@NonNull List<AccessibilityWindowInfo> windows,
            int windowId) {
        for (AccessibilityWindowInfo window : windows) {
            if (window.getId() == windowId) {
                return window;
            }
        }
        return null;
    }

    /** Gets the bounds in screen of the given {@code node}. */
    @NonNull
    static Rect getBoundsInScreen(@NonNull AccessibilityNodeInfo node) {
        Rect bounds = new Rect();
        node.getBoundsInScreen(bounds);
        if (Utils.isFocusArea(node)) {
            // For a FocusArea, the bounds used for finding the nudge target are its View bounds
            // minus the offset.
            Bundle bundle = node.getExtras();
            bounds.left += bundle.getInt(FOCUS_AREA_LEFT_BOUND_OFFSET);
            bounds.right -= bundle.getInt(FOCUS_AREA_RIGHT_BOUND_OFFSET);
            bounds.top += bundle.getInt(FOCUS_AREA_TOP_BOUND_OFFSET);
            bounds.bottom -= bundle.getInt(FOCUS_AREA_BOTTOM_BOUND_OFFSET);
        } else if (Utils.isScrollableContainer(node)) {
            // For a scrollable container, the bounds used for finding the nudge target are the
            // minimum bounds containing its children.
            bounds.setEmpty();
            Rect childBounds = new Rect();
            for (int i = 0; i < node.getChildCount(); i++) {
                AccessibilityNodeInfo child = node.getChild(i);
                if (child != null) {
                    child.getBoundsInScreen(childBounds);
                    child.recycle();
                    bounds.union(childBounds);
                }
            }
        }
        return bounds;
    }
}
