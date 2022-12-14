/*
 * Copyright (C) 2017 The Android Open Source Project
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

package com.android.car.settings.datetime;

import android.app.timedetector.ManualTimeSuggestion;
import android.app.timedetector.TimeDetector;
import android.content.Intent;
import android.os.Bundle;
import android.text.format.DateFormat;
import android.widget.TimePicker;

import androidx.annotation.LayoutRes;
import androidx.annotation.StringRes;

import com.android.car.settings.R;
import com.android.car.settings.common.BaseFragment;
import com.android.car.ui.toolbar.MenuItem;

import java.util.Calendar;
import java.util.Collections;
import java.util.List;

/**
 * Sets the system time.
 */
public class TimePickerFragment extends BaseFragment {
    private static final int MILLIS_IN_SECOND = 1000;

    private TimePicker mTimePicker;
    private MenuItem mOkButton;

    @Override
    public List<MenuItem> getToolbarMenuItems() {
        return Collections.singletonList(mOkButton);
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        mOkButton = new MenuItem.Builder(getContext())
                .setTitle(android.R.string.ok)
                .setOnClickListener(i -> {
                    Calendar c = Calendar.getInstance();
                    c.set(Calendar.HOUR_OF_DAY, mTimePicker.getHour());
                    c.set(Calendar.MINUTE, mTimePicker.getMinute());
                    c.set(Calendar.SECOND, 0);
                    c.set(Calendar.MILLISECOND, 0);
                    long when = Math.max(c.getTimeInMillis(), DatetimeSettingsFragment.MIN_DATE);
                    if (when / MILLIS_IN_SECOND < Integer.MAX_VALUE) {
                        TimeDetector timeDetector =
                                getContext().getSystemService(TimeDetector.class);
                        ManualTimeSuggestion manualTimeSuggestion =
                                TimeDetector.createManualTimeSuggestion(when, "Settings: Set time");
                        timeDetector.suggestManualTime(manualTimeSuggestion);
                        getContext().sendBroadcast(new Intent(Intent.ACTION_TIME_CHANGED));
                    }
                    getFragmentHost().goBack();
                })
                .build();
    }

    @Override
    @LayoutRes
    protected int getLayoutId() {
        return R.layout.time_picker;
    }

    @Override
    @StringRes
    protected int getTitleId() {
        return R.string.time_picker_title;
    }

    @Override
    public void onActivityCreated(Bundle savedInstanceState) {
        super.onActivityCreated(savedInstanceState);

        mTimePicker = (TimePicker) getView().findViewById(R.id.time_picker);
        mTimePicker.setIs24HourView(is24Hour());
    }

    private boolean is24Hour() {
        return DateFormat.is24HourFormat(getContext());
    }
}
