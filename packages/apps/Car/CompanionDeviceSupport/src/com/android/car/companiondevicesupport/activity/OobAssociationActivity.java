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

package com.android.car.companiondevicesupport.activity;

import static com.android.car.connecteddevice.util.SafeLog.loge;
import static com.android.car.ui.core.CarUi.requireToolbar;
import static com.android.car.ui.toolbar.Toolbar.State.SUBPAGE;

import android.app.Activity;
import android.os.Bundle;
import android.widget.Toast;

import androidx.fragment.app.FragmentActivity;
import androidx.lifecycle.ViewModelProvider;
import androidx.lifecycle.ViewModelStore;

import com.android.car.companiondevicesupport.R;
import com.android.car.companiondevicesupport.api.internal.association.OobEligibleDevice;
import com.android.car.ui.toolbar.ToolbarController;

import com.google.common.base.Strings;

public class OobAssociationActivity extends FragmentActivity {
    private static final String TAG = "OobAssociationActivity";
    private static final String OOB_ADD_DEVICE_FRAGMENT_TAG = "OobAddAssociatedDeviceFragment";

    private static final String EXTRA_DEVICE_ADDRESS = "DEVICE_ADDRESS";

    private ToolbarController mToolbar;
    private OobAssociatedDeviceViewModel mModel;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.base_activity);

        String deviceAddress = getIntent().getStringExtra(EXTRA_DEVICE_ADDRESS);
        if (Strings.isNullOrEmpty(deviceAddress)) {
            loge(TAG, "Activity was started with an intent that didn't " +
                    "specify the device to connect to, finishing");

            setResult(Activity.RESULT_CANCELED);
            finish();
            return;
        }

        observeViewModel(deviceAddress);

        mToolbar = requireToolbar(this);
        mToolbar.setState(SUBPAGE);

        getSupportFragmentManager()
                .beginTransaction()
                .replace(R.id.fragment_container, new OobAddAssociatedDeviceFragment(),
                        OOB_ADD_DEVICE_FRAGMENT_TAG)
                .commit();
        mToolbar.getProgressBar().setVisible(true);
    }

    @Override
    protected void onStart() {
        super.onStart();
        mModel.startAssociation();
    }

    @Override
    protected void onStop() {
        super.onStop();
        mModel.stopAssociation();
    }

    @Override
    protected void onDestroy() {
        mToolbar.getProgressBar().setVisible(false);
        super.onDestroy();
    }

    private void observeViewModel(String deviceAddress) {
        mModel = new ViewModelProvider(ViewModelStore::new,
                OobAssociatedDeviceViewModelFactory.getInstance(getApplication(),
                        new OobEligibleDevice(deviceAddress,
                                com.android.car.connecteddevice.model.OobEligibleDevice.
                                        OOB_TYPE_BLUETOOTH))).
                get(OobAssociatedDeviceViewModel.class);

        mModel.getAssociationState().observe(this, state -> {
            switch (state) {
                case COMPLETED:
                    mModel.resetAssociationState();
                    runOnUiThread(() -> Toast.makeText(getApplicationContext(),
                            getString(R.string.continue_setup_toast_text),
                            Toast.LENGTH_SHORT).show());
                    setResult(Activity.RESULT_OK);
                    finish();
                    break;
                case PENDING:
                case ERROR:
                case NONE:
                case STARTING:
                case STARTED:
                    break;
                default:
                    loge(TAG, "Encountered unexpected association state: " + state);
            }
        });
    }
}
