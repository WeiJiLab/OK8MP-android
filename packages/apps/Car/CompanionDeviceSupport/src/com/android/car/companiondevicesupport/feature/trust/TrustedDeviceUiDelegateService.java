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

package com.android.car.companiondevicesupport.feature.trust;

import static com.android.car.connecteddevice.util.SafeLog.logd;
import static com.android.car.connecteddevice.util.SafeLog.loge;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.IBinder;
import android.os.RemoteException;

import androidx.annotation.Nullable;
import androidx.core.content.ContextCompat;

import com.android.car.companiondevicesupport.R;
import com.android.car.companiondevicesupport.api.internal.trust.ITrustedDeviceManager;
import com.android.car.companiondevicesupport.api.internal.trust.IOnTrustedDeviceEnrollmentNotificationRequestListener;
import com.android.car.companiondevicesupport.feature.trust.ui.TrustedDeviceActivity;

/** Service to provide UI functionality to the {@link TrustedDeviceManagerService}. */
public class TrustedDeviceUiDelegateService extends Service {
    private static final String TAG = "TrustedDeviceUiDelegateService";
    private static final String CHANNEL_ID = "trusteddevice_notification_channel";
    private static final int ENROLLMENT_NOTIFICATION_ID = 0;

    private NotificationManager mNotificationManager;
    private ITrustedDeviceManager mTrustedDeviceManager;

    @Override
    public void onCreate() {
        super.onCreate();
        Intent intent = new Intent(this, TrustedDeviceManagerService.class);
        bindService(intent, mServiceConnection, Context.BIND_AUTO_CREATE);
        mNotificationManager = (NotificationManager) getBaseContext().
                getSystemService(Context.NOTIFICATION_SERVICE);
        String channelName = this.getString(R.string.trusted_device_notification_channel_name);
        NotificationChannel channel = new NotificationChannel(CHANNEL_ID, channelName,
                NotificationManager.IMPORTANCE_HIGH);
        mNotificationManager.createNotificationChannel(channel);
        logd(TAG, "Service created.");
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        unregisterCallbacks();
    }

    private void showEnrollmentNotification() {
        if (mNotificationManager == null) {
            loge(TAG, "Failed to start enrollment notification, notification manager is null.");
            return;
        }
        Intent enrollmentIntent = new Intent(this, TrustedDeviceActivity.class)
                // Setting this action ensures that the TrustedDeviceActivity is resumed if it is
                // already running.
                .setAction("com.android.settings.action.EXTRA_SETTINGS")
                .putExtra(TrustedDeviceConstants.INTENT_EXTRA_ENROLL_NEW_TOKEN, true)
                .setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        PendingIntent pendingIntent = PendingIntent
                .getActivity(this, /* requestCode = */ 0, enrollmentIntent,
                PendingIntent.FLAG_UPDATE_CURRENT);
        Notification notification = new Notification.Builder(this, CHANNEL_ID)
                .setSmallIcon(R.drawable.ic_directions_car_filled)
                .setColor(ContextCompat.getColor(getBaseContext(), R.color.car_red_300))
                .setContentTitle(this.getString(R.string.trusted_device_notification_title))
                .setContentText(this.getString(R.string.trusted_device_notification_content))
                .setContentIntent(pendingIntent)
                .setAutoCancel(true)
                .build();
        mNotificationManager.notify(/* tag = */ null, ENROLLMENT_NOTIFICATION_ID,
                notification);
    }

    private void registerCallbacks() {
        if (mTrustedDeviceManager == null) {
            loge(TAG, "Failed to register callbacks, service not connected.");
            return;
        }
        try {
            mTrustedDeviceManager.registerTrustedDeviceEnrollmentNotificationRequestListener(
                    mEnrollmentNotificationListener);
        } catch (RemoteException e) {
            loge(TAG, "Failed to register enrollment callback.", e);
        }
    }

    private void unregisterCallbacks() {
        if (mTrustedDeviceManager == null) {
            loge(TAG, "Failed to unregister callbacks, service not connected.");
            return;
        }
        try {
            mTrustedDeviceManager.unregisterTrustedDeviceEnrollmentNotificationRequestListener(
                    mEnrollmentNotificationListener);
        } catch (RemoteException e) {
            loge(TAG, "Failed to unregister callbacks.", e);
        }
    }

    @Nullable
    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private final ServiceConnection mServiceConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder service) {
            mTrustedDeviceManager = ITrustedDeviceManager.Stub.asInterface(service);
            registerCallbacks();
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            mTrustedDeviceManager = null;
        }
    };

    private final IOnTrustedDeviceEnrollmentNotificationRequestListener
            mEnrollmentNotificationListener =
            new IOnTrustedDeviceEnrollmentNotificationRequestListener.Stub() {
        @Override
        public void onTrustedDeviceEnrollmentNotificationRequest() {
            showEnrollmentNotification();
        }
    };
}
