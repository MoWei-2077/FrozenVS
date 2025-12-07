package com.android.server.display;

import android.animation.Animator;
import android.animation.ObjectAnimator;
import android.app.ActivityManager;
import android.content.Context;
import android.content.pm.ParceledListSlice;
import android.content.res.Resources;
import android.database.ContentObserver;
import android.hardware.Sensor;
import android.hardware.SensorManager;
import android.hardware.display.AmbientBrightnessDayStats;
import android.hardware.display.BrightnessChangeEvent;
import android.hardware.display.BrightnessConfiguration;
import android.hardware.display.BrightnessInfo;
import android.hardware.display.DisplayManagerInternal;
import android.metrics.LogMaker;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerExecutor;
import android.os.IBinder;
import android.os.Looper;
import android.os.Message;
import android.os.PowerManager;
import android.os.RemoteException;
import android.os.SystemClock;
import android.os.SystemProperties;
import android.os.Trace;
import android.provider.Settings;
import android.util.FloatProperty;
import android.util.IndentingPrintWriter;
import android.util.MathUtils;
import android.util.MutableFloat;
import android.util.MutableInt;
import android.util.Slog;
import android.util.SparseArray;
import android.view.Display;
import com.android.internal.app.IBatteryStats;
import com.android.internal.logging.MetricsLogger;
import com.android.internal.util.FrameworkStatsLog;
import com.android.internal.util.RingBuffer;
import com.android.server.LocalServices;
import com.android.server.am.BatteryStatsService;
import com.android.server.display.AutomaticBrightnessController;
import com.android.server.display.BrightnessSetting;
import com.android.server.display.DisplayDeviceConfig;
import com.android.server.display.DisplayPowerController;
import com.android.server.display.HighBrightnessModeController;
import com.android.server.display.RampAnimator;
import com.android.server.display.ScreenOffBrightnessSensorController;
import com.android.server.display.brightness.BrightnessEvent;
import com.android.server.display.brightness.BrightnessReason;
import com.android.server.display.brightness.BrightnessUtils;
import com.android.server.display.brightness.DisplayBrightnessController;
import com.android.server.display.brightness.clamper.BrightnessClamperController;
import com.android.server.display.brightness.strategy.AutomaticBrightnessStrategy;
import com.android.server.display.color.ColorDisplayService;
import com.android.server.display.config.DisplayBrightnessMappingConfig;
import com.android.server.display.config.HysteresisLevels;
import com.android.server.display.feature.DisplayManagerFlags;
import com.android.server.display.state.DisplayStateController;
import com.android.server.display.utils.DebugUtils;
import com.android.server.display.utils.SensorUtils;
import com.android.server.display.whitebalance.DisplayWhiteBalanceController;
import com.android.server.display.whitebalance.DisplayWhiteBalanceFactory;
import com.android.server.display.whitebalance.DisplayWhiteBalanceSettings;
import com.android.server.policy.WindowManagerPolicy;
import com.oplus.util.OplusPlatformLevelUtils;
import java.io.PrintWriter;
import java.util.Objects;
import system.ext.loader.core.ExtLoader;
/* loaded from: classes2.dex */
public final class DisplayPowerController implements AutomaticBrightnessController.Callbacks, DisplayWhiteBalanceController.Callbacks, DisplayPowerControllerInterface {
    private static final int BRIGHTNESS_CHANGE_STATSD_REPORT_INTERVAL_MS = 500;
    private static final int COLOR_FADE_OFF_ANIMATION_DURATION_MILLIS = 100;
    private static final int COLOR_FADE_ON_ANIMATION_DURATION_MILLIS = 250;
    private static final String GLOBAL_HBM_SELL_MODE = "global_hbm_sell_mode";
    private static final int MSG_BOOT_COMPLETED = 13;
    private static final int MSG_BRIGHTNESS_RAMP_DONE = 10;
    private static final int MSG_CONFIGURE_BRIGHTNESS = 4;
    private static final int MSG_OFFLOADING_SCREEN_ON_UNBLOCKED = 18;
    private static final int MSG_RESET_FPS_AFTER_FINISH_DC_BRIGHTNESS = 21;
    private static final int MSG_RESET_SCREEN_ON_CABC = 22;
    private static final int MSG_SCREEN_OFF_UNBLOCKED = 3;
    private static final int MSG_SCREEN_ON_UNBLOCKED = 2;
    private static final int MSG_SET_BRIGHTNESS_FROM_OFFLOAD = 17;
    private static final int MSG_SET_DWBC_COLOR_OVERRIDE = 15;
    private static final int MSG_SET_DWBC_LOGGING_ENABLED = 16;
    private static final int MSG_SET_TEMPORARY_AUTO_BRIGHTNESS_ADJUSTMENT = 6;
    private static final int MSG_SET_TEMPORARY_BRIGHTNESS = 5;
    private static final int MSG_STATSD_HBM_BRIGHTNESS = 11;
    private static final int MSG_STOP = 7;
    private static final int MSG_SWITCH_AUTOBRIGHTNESS_MODE = 14;
    private static final int MSG_SWITCH_USER = 12;
    private static final int MSG_UPDATE_BRIGHTNESS = 8;
    private static final int MSG_UPDATE_POWER_STATE = 1;
    private static final int MSG_UPDATE_RBC = 9;
    private static final int RAMP_STATE_SKIP_AUTOBRIGHT = 2;
    private static final int RAMP_STATE_SKIP_INITIAL = 1;
    private static final int RAMP_STATE_SKIP_NONE = 0;
    private static final int REPORTED_TO_POLICY_SCREEN_OFF = 0;
    private static final int REPORTED_TO_POLICY_SCREEN_ON = 2;
    private static final int REPORTED_TO_POLICY_SCREEN_TURNING_OFF = 3;
    private static final int REPORTED_TO_POLICY_SCREEN_TURNING_ON = 1;
    private static final int REPORTED_TO_POLICY_UNREPORTED = -1;
    private static final int RINGBUFFER_MAX = 100;
    private static final int RINGBUFFER_RBC_MAX = 20;
    private static final float SCREEN_ANIMATION_RATE_MINIMUM = 0.0f;
    private static final String SCREEN_OFF_BLOCKED_TRACE_NAME = "Screen off blocked";
    private static final String SCREEN_ON_BLOCKED_BY_DISPLAYOFFLOAD_TRACE_NAME = "Screen on blocked by displayoffload";
    private static final String SCREEN_ON_BLOCKED_TRACE_NAME = "Screen on blocked";
    private static final String SECOND_SCREEN_AUTO_BRIGHTNESS_ADJ = "second_screen_auto_brightness_adj";
    private static final String UNBLOCK_REASON_GO_TO_SLEEP = "UNBLOCK_REASON_GO_TO_SLEEP";
    private static final boolean USE_COLOR_FADE_ON_ANIMATION = false;
    private boolean isRM;
    private boolean mAppliedDimming;
    private boolean mAppliedThrottling;
    private IColorAutomaticBrightnessController mAutomaticBrightnessController;
    private final AutomaticBrightnessStrategy mAutomaticBrightnessStrategy;
    private final IBatteryStats mBatteryStats;
    private final DisplayBlanker mBlanker;
    private boolean mBootCompleted;
    private final boolean mBrightnessBucketsInDozeConfig;
    private final BrightnessClamperController mBrightnessClamperController;
    private RingBuffer<BrightnessEvent> mBrightnessEventRingBuffer;
    private long mBrightnessRampDecreaseMaxTimeIdleMillis;
    private long mBrightnessRampDecreaseMaxTimeMillis;
    private long mBrightnessRampIncreaseMaxTimeIdleMillis;
    private long mBrightnessRampIncreaseMaxTimeMillis;
    private float mBrightnessRampRateFastDecrease;
    private float mBrightnessRampRateFastIncrease;
    private float mBrightnessRampRateSlowDecrease;
    private float mBrightnessRampRateSlowDecreaseIdle;
    private float mBrightnessRampRateSlowIncrease;
    private float mBrightnessRampRateSlowIncreaseIdle;
    private final BrightnessRangeController mBrightnessRangeController;
    private final BrightnessThrottler mBrightnessThrottler;
    private final BrightnessTracker mBrightnessTracker;
    private final ColorDisplayService.ColorDisplayServiceInternal mCdsi;
    private final Clock mClock;
    private final boolean mColorFadeEnabled;
    private final boolean mColorFadeFadesConfig;
    private ObjectAnimator mColorFadeOffAnimator;
    private ObjectAnimator mColorFadeOnAnimator;
    private final Context mContext;
    private final boolean mDisplayBlanksAfterDozeConfig;
    private final DisplayBrightnessController mDisplayBrightnessController;
    private DisplayDevice mDisplayDevice;
    private DisplayDeviceConfig mDisplayDeviceConfig;
    private final int mDisplayId;
    private DisplayManagerInternal.DisplayOffloadSession mDisplayOffloadSession;
    private final DisplayPowerProximityStateController mDisplayPowerProximityStateController;
    private boolean mDisplayReadyLocked;
    private final DisplayStateController mDisplayStateController;
    private int mDisplayStatsId;
    private final DisplayWhiteBalanceController mDisplayWhiteBalanceController;
    private final DisplayWhiteBalanceSettings mDisplayWhiteBalanceSettings;
    private float mDozeScaleFactor;
    private boolean mDozing;
    public IOplusDisplayPowerControllerExt mDpcExt;
    private final DisplayManagerFlags mFlags;
    private final DisplayControllerHandler mHandler;
    private float mInitialAutoBrightness;
    private final Injector mInjector;
    private boolean mIsDisplayInternal;
    private boolean mIsEnabled;
    private boolean mIsInTransition;
    private boolean mIsPrimaryDisplay;
    private boolean mIsRbcActive;
    private final BrightnessEvent mLastBrightnessEvent;
    private int mLastState;
    private Sensor mLightSensor;
    private final LogicalDisplay mLogicalDisplay;
    private float[] mNitsRange;
    private final Runnable mOnBrightnessChangeRunnable;
    private boolean mPendingRequestChangedLocked;
    private DisplayManagerInternal.DisplayPowerRequest mPendingRequestLocked;
    private boolean mPendingScreenOff;
    private ScreenOffUnblocker mPendingScreenOffUnblocker;
    private ScreenOnUnblocker mPendingScreenOnUnblocker;
    private Runnable mPendingScreenOnUnblockerByDisplayOffload;
    private boolean mPendingUpdatePowerStateLocked;
    private DisplayManagerInternal.DisplayPowerRequest mPowerRequest;
    private DisplayPowerState mPowerState;
    private float mScreenBrightnessDefault;
    private final float mScreenBrightnessDozeConfig;
    private float mScreenBrightnessNormalMaximum;
    private RampAnimator.DualRampAnimator<DisplayPowerState> mScreenBrightnessRampAnimator;
    private float mScreenBrightnessRangeMaximum;
    private float mScreenBrightnessRangeMinimum;
    private long mScreenOffBlockStartRealTime;
    private Sensor mScreenOffBrightnessSensor;
    private ScreenOffBrightnessSensorController mScreenOffBrightnessSensorController;
    private long mScreenOnBlockByDisplayOffloadStartRealTime;
    private long mScreenOnBlockStartRealTime;
    private boolean mScreenTurningOnWasBlockedByDisplayOffload;
    private final SensorManager mSensorManager;
    private final SettingsObserver mSettingsObserver;
    private final boolean mSkipScreenOnBrightnessRamp;
    private boolean mStopped;
    private final String mTag;
    private final BrightnessEvent mTempBrightnessEvent;
    private String mThermalBrightnessThrottlingDataId;
    private String mUniqueDisplayId;
    private boolean mUseSoftwareAutoBrightnessConfig;
    private final WakelockController mWakelockController;
    private final WindowManagerPolicy mWindowManagerPolicy;
    private static final String TAG = "DisplayPowerController";
    private static boolean DEBUG = DebugUtils.isDebuggable(TAG);
    private static final boolean MTK_DEBUG = "eng".equals(Build.TYPE);
    private static final int DC_MODE_BRIGHT_EDGE = SystemProperties.getInt("ro.vendor.display.dc.brightness.threshold", 260);
    private static final String DC_MODE_CUSTOMIZATION_KEY = "ro.vendor.display.dc.brightness.customization";
    private static final boolean DC_MODE_BRIGHT_CUSTOMIZATION = SystemProperties.getBoolean(DC_MODE_CUSTOMIZATION_KEY, false);
    private static final boolean IS_LIGHT_OS_BY_AMS = SystemProperties.getBoolean("ro.oplus.lightos.ams", false);
    private static boolean DEBUG_PANIC = false;
    private static final float[] BRIGHTNESS_RANGE_BOUNDARIES = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 80.0f, 90.0f, 100.0f, 200.0f, 300.0f, 400.0f, 500.0f, 600.0f, 700.0f, 800.0f, 900.0f, 1000.0f, 1200.0f, 1400.0f, 1600.0f, 1800.0f, 2000.0f, 2250.0f, 2500.0f, 2750.0f, 3000.0f};
    private static final int[] BRIGHTNESS_RANGE_INDEX = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37};
    private static boolean DisplayDisable = SystemProperties.getBoolean("ro.oplus.display.fingerprint_disable", false);
    private static final String FP_SENSOR_TYPE = SystemProperties.get("persist.vendor.fingerprint.sensor_type", "unknow");
    private final Object mLock = new Object();
    private int mLeadDisplayId = -1;
    private final CachedBrightnessInfo mCachedBrightnessInfo = new CachedBrightnessInfo();
    private int mReportedScreenStateToPolicy = -1;
    final BrightnessReason mBrightnessReason = new BrightnessReason();
    private final BrightnessReason mBrightnessReasonTemp = new BrightnessReason();
    private float mLastStatsBrightness = 0.0f;
    private final RingBuffer<BrightnessEvent> mRbcEventRingBuffer = new RingBuffer<>(BrightnessEvent.class, 20);
    private int mSkipRampState = 0;
    private boolean mIsUserSwitching = false;
    private boolean mDCBrightnessChange = false;
    private boolean mUpdateFpsForDc = false;
    private boolean mResetFpsStatePending = false;
    private SparseArray<DisplayPowerControllerInterface> mDisplayBrightnessFollowers = new SparseArray<>();
    private final Animator.AnimatorListener mAnimatorListener = new Animator.AnimatorListener() { // from class: com.android.server.display.DisplayPowerController.2
        @Override // android.animation.Animator.AnimatorListener
        public void onAnimationStart(Animator animation) {
        }

        @Override // android.animation.Animator.AnimatorListener
        public void onAnimationEnd(Animator animation) {
            DisplayPowerController.this.sendUpdatePowerState();
            DisplayPowerController.this.mDpcExt.onAnimationChanged(animation, 2);
        }

        @Override // android.animation.Animator.AnimatorListener
        public void onAnimationRepeat(Animator animation) {
        }

        @Override // android.animation.Animator.AnimatorListener
        public void onAnimationCancel(Animator animation) {
        }
    };
    private final RampAnimator.Listener mRampAnimatorListener = new RampAnimator.Listener() { // from class: com.android.server.display.DisplayPowerController.3
        @Override // com.android.server.display.RampAnimator.Listener
        public void onAnimationStart(boolean isPrimaryAnimator) {
            DisplayPowerController.this.mDpcExt.setAnimating(true, isPrimaryAnimator);
        }

        @Override // com.android.server.display.RampAnimator.Listener
        public void onAnimationEnd(boolean isPrimaryAnimator) {
            DisplayPowerController.this.sendUpdatePowerState();
            if (isPrimaryAnimator) {
                DisplayPowerController.this.updateFpsIfNeeded(DisplayPowerController.this.mDpcExt.getMaximumScreenBrightnessSetting());
                DisplayPowerController.this.mDpcExt.setLowPowerAnimatingState(false);
                DisplayPowerController.this.mDpcExt.setHDRAnimatingState(false);
                Message msg = DisplayPowerController.this.mHandler.obtainMessage(10);
                DisplayPowerController.this.mHandler.sendMessageAtTime(msg, DisplayPowerController.this.mClock.uptimeMillis());
            }
            DisplayPowerController.this.mDpcExt.setAnimating(false, isPrimaryAnimator);
        }
    };
    private final Runnable mCleanListener = new Runnable() { // from class: com.android.server.display.DisplayPowerController$$ExternalSyntheticLambda7
        @Override // java.lang.Runnable
        public final void run() {
            DisplayPowerController.this.sendUpdatePowerState();
        }
    };
    private final IOplusDisplayPowerControllerWrapper mWrapper = new OplusDisplayPowerControllerWrapper();

    /* JADX INFO: Access modifiers changed from: package-private */
    /* loaded from: classes2.dex */
    public interface Clock {
        long uptimeMillis();
    }

    /* JADX INFO: Access modifiers changed from: package-private */
    public DisplayPowerController(Context context, Injector injector, DisplayManagerInternal.DisplayPowerCallbacks callbacks, Handler handler, SensorManager sensorManager, DisplayBlanker blanker, LogicalDisplay logicalDisplay, BrightnessTracker brightnessTracker, BrightnessSetting brightnessSetting, Runnable onBrightnessChangeRunnable, HighBrightnessModeMetadata hbmMetadata, boolean bootCompleted, DisplayManagerFlags flags) {
        this.mIsPrimaryDisplay = false;
        this.mFlags = flags;
        this.mInjector = injector != null ? injector : new Injector();
        this.mClock = this.mInjector.getClock();
        this.mLogicalDisplay = logicalDisplay;
        this.mDisplayId = this.mLogicalDisplay.getDisplayIdLocked();
        this.mSensorManager = sensorManager;
        this.mHandler = new DisplayControllerHandler(handler.getLooper());
        this.mDisplayDeviceConfig = logicalDisplay.getPrimaryDisplayDeviceLocked().getDisplayDeviceConfig();
        this.mIsEnabled = logicalDisplay.isEnabledLocked();
        this.mIsInTransition = logicalDisplay.isInTransitionLocked();
        this.mIsDisplayInternal = logicalDisplay.getPrimaryDisplayDeviceLocked().getDisplayDeviceInfoLocked().type == 1;
        this.mDpcExt = (IOplusDisplayPowerControllerExt) ExtLoader.type(IOplusDisplayPowerControllerExt.class).base(this).create();
        this.mWakelockController = this.mInjector.getWakelockController(this.mDisplayId, callbacks);
        this.mDisplayPowerProximityStateController = this.mInjector.getDisplayPowerProximityStateController(this.mWakelockController, this.mDisplayDeviceConfig, this.mHandler.getLooper(), new Runnable() { // from class: com.android.server.display.DisplayPowerController$$ExternalSyntheticLambda8
            @Override // java.lang.Runnable
            public final void run() {
                DisplayPowerController.this.lambda$new$0();
            }
        }, this.mDisplayId, this.mSensorManager, this.mDpcExt);
        this.mDisplayStateController = new DisplayStateController(this.mDisplayPowerProximityStateController, this.mDpcExt);
        this.mTag = "DisplayPowerController[" + this.mDisplayId + "]";
        this.mThermalBrightnessThrottlingDataId = logicalDisplay.getDisplayInfoLocked().thermalBrightnessThrottlingDataId;
        this.mDisplayDevice = this.mLogicalDisplay.getPrimaryDisplayDeviceLocked();
        this.mUniqueDisplayId = logicalDisplay.getPrimaryDisplayDeviceLocked().getUniqueId();
        this.mDisplayStatsId = this.mUniqueDisplayId.hashCode();
        this.mLastBrightnessEvent = new BrightnessEvent(this.mDisplayId);
        this.mTempBrightnessEvent = new BrightnessEvent(this.mDisplayId);
        if (this.mDisplayId == 0) {
            this.mBatteryStats = BatteryStatsService.getService();
        } else {
            this.mBatteryStats = null;
        }
        this.mSettingsObserver = new SettingsObserver(this.mHandler);
        this.mWindowManagerPolicy = (WindowManagerPolicy) LocalServices.getService(WindowManagerPolicy.class);
        this.mBlanker = blanker;
        this.mContext = context;
        this.mBrightnessTracker = brightnessTracker;
        this.mLightSensor = this.mSensorManager.getDefaultSensor(5);
        this.mDpcExt.init(this.mContext, this.mDisplayId);
        if (this.mDisplayId == 0) {
            this.mDpcExt.setDisplayPowerController(this);
            this.mDpcExt.setOplusDisplayPowerControllerCallback(callbacks);
            this.mDpcExt.setDisplayPowerControlHandler(handler);
        }
        this.mOnBrightnessChangeRunnable = onBrightnessChangeRunnable;
        PowerManager pm = (PowerManager) context.getSystemService(PowerManager.class);
        Resources resources = context.getResources();
        this.mScreenBrightnessDozeConfig = BrightnessUtils.clampAbsoluteBrightness(pm.getBrightnessConstraint(4));
        loadBrightnessRampRates();
        this.mSkipScreenOnBrightnessRamp = resources.getBoolean(17891881);
        this.mDozeScaleFactor = context.getResources().getFraction(18022407, 1, 1);
        Runnable modeChangeCallback = new Runnable() { // from class: com.android.server.display.DisplayPowerController$$ExternalSyntheticLambda9
            @Override // java.lang.Runnable
            public final void run() {
                DisplayPowerController.this.lambda$new$1();
            }
        };
        HighBrightnessModeController hbmController = createHbmControllerLocked(hbmMetadata, modeChangeCallback);
        this.mBrightnessThrottler = createBrightnessThrottlerLocked();
        this.mBrightnessRangeController = this.mInjector.getBrightnessRangeController(hbmController, modeChangeCallback, this.mDisplayDeviceConfig, this.mHandler, flags, this.mDisplayDevice.getDisplayTokenLocked(), this.mDisplayDevice.getDisplayDeviceInfoLocked());
        this.mDisplayBrightnessController = new DisplayBrightnessController(context, null, this.mDisplayId, this.mLogicalDisplay.getDisplayInfoLocked().brightnessDefault, brightnessSetting, new Runnable() { // from class: com.android.server.display.DisplayPowerController$$ExternalSyntheticLambda10
            @Override // java.lang.Runnable
            public final void run() {
                DisplayPowerController.this.lambda$new$2();
            }
        }, new HandlerExecutor(this.mHandler), flags, this.mDpcExt);
        Injector injector2 = this.mInjector;
        DisplayControllerHandler displayControllerHandler = this.mHandler;
        Objects.requireNonNull(modeChangeCallback);
        this.mBrightnessClamperController = injector2.getBrightnessClamperController(displayControllerHandler, new BrightnessRangeController$$ExternalSyntheticLambda5(modeChangeCallback), new BrightnessClamperController.DisplayDeviceData(this.mUniqueDisplayId, this.mThermalBrightnessThrottlingDataId, logicalDisplay.getPowerThrottlingDataIdLocked(), this.mDisplayDeviceConfig), this.mContext, flags, this.mSensorManager, this.mDpcExt);
        saveBrightnessInfo(getScreenBrightnessSetting());
        this.mAutomaticBrightnessStrategy = this.mDisplayBrightnessController.getAutomaticBrightnessStrategy();
        DisplayWhiteBalanceSettings displayWhiteBalanceSettings = null;
        DisplayWhiteBalanceController displayWhiteBalanceController = null;
        if (this.mDisplayId == 0) {
            try {
                displayWhiteBalanceController = this.mInjector.getDisplayWhiteBalanceController(this.mHandler, this.mSensorManager, resources);
                displayWhiteBalanceSettings = new DisplayWhiteBalanceSettings(this.mContext, this.mHandler);
                displayWhiteBalanceSettings.setCallbacks(this);
                displayWhiteBalanceController.setCallbacks(this);
            } catch (Exception e) {
                Slog.e(this.mTag, "failed to set up display white-balance: " + e);
            }
        }
        this.mDisplayWhiteBalanceSettings = displayWhiteBalanceSettings;
        this.mDisplayWhiteBalanceController = displayWhiteBalanceController;
        loadNitsRange(resources);
        if (this.mDisplayId == 0) {
            this.mCdsi = (ColorDisplayService.ColorDisplayServiceInternal) LocalServices.getService(ColorDisplayService.ColorDisplayServiceInternal.class);
            if (this.mCdsi != null) {
                boolean active = this.mCdsi.setReduceBrightColorsListener(new ColorDisplayService.ReduceBrightColorsListener() { // from class: com.android.server.display.DisplayPowerController.1
                    @Override // com.android.server.display.color.ColorDisplayService.ReduceBrightColorsListener
                    public void onReduceBrightColorsActivationChanged(boolean activated, boolean userInitiated) {
                        DisplayPowerController.this.applyReduceBrightColorsSplineAdjustment();
                    }

                    @Override // com.android.server.display.color.ColorDisplayService.ReduceBrightColorsListener
                    public void onReduceBrightColorsStrengthChanged(int strength) {
                        DisplayPowerController.this.applyReduceBrightColorsSplineAdjustment();
                    }
                });
                if (active) {
                    applyReduceBrightColorsSplineAdjustment();
                }
            }
        } else {
            this.mCdsi = null;
        }
        setUpAutoBrightness(context, handler);
        this.mColorFadeEnabled = this.mInjector.isColorFadeEnabled() && !resources.getBoolean(17891654);
        this.mColorFadeFadesConfig = resources.getBoolean(17891377);
        this.mDisplayBlanksAfterDozeConfig = resources.getBoolean(17891652);
        this.mBrightnessBucketsInDozeConfig = resources.getBoolean(17891653);
        this.mDpcExt.initParameters(this.mHandler);
        this.mIsPrimaryDisplay = this.mDpcExt.isPrimaryDisplay(this.mUniqueDisplayId);
        this.mDpcExt.setUniqueDisplayId(this.mIsPrimaryDisplay, this.mUniqueDisplayId);
        this.mDpcExt.setDCMode();
        Slog.d(this.mTag, "DPC construct " + this.mUniqueDisplayId + " mIsPrimaryDisplay:" + this.mIsPrimaryDisplay);
        String isReset = SystemProperties.get("debug.display.cabc.reset", "0");
        this.isRM = "1".equals(isReset);
        this.mBootCompleted = bootCompleted;
    }

    /* JADX INFO: Access modifiers changed from: private */
    public /* synthetic */ void lambda$new$1() {
        sendUpdatePowerState();
        lambda$new$2();
    }

    /* JADX INFO: Access modifiers changed from: private */
    public void applyReduceBrightColorsSplineAdjustment() {
        this.mHandler.obtainMessage(9).sendToTarget();
        sendUpdatePowerState();
    }

    /* JADX INFO: Access modifiers changed from: private */
    public void handleRbcChanged() {
        if (this.mAutomaticBrightnessController == null) {
            return;
        }
        float[] adjustedNits = new float[this.mNitsRange.length];
        for (int i = 0; i < this.mNitsRange.length; i++) {
            adjustedNits[i] = this.mCdsi.getReduceBrightColorsAdjustedBrightnessNits(this.mNitsRange[i]);
        }
        this.mIsRbcActive = this.mCdsi.isReduceBrightColorsActivated();
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public boolean isProximitySensorAvailable() {
        return this.mDisplayPowerProximityStateController.isProximitySensorAvailable();
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public ParceledListSlice<BrightnessChangeEvent> getBrightnessEvents(int userId, boolean includePackage) {
        if (this.mBrightnessTracker == null) {
            return null;
        }
        return this.mBrightnessTracker.getEvents(userId, includePackage);
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void onSwitchUser(int newUserId, int userSerial, float newBrightness) {
        float currentBrightness = this.mDisplayBrightnessController.getCurrentBrightness();
        Message msg = this.mHandler.obtainMessage(12, newUserId, userSerial, Float.valueOf(currentBrightness));
        this.mHandler.sendMessageAtTime(msg, this.mClock.uptimeMillis());
    }

    /* JADX INFO: Access modifiers changed from: private */
    public void handleOnSwitchUser(int newUserId, int userSerial, float newBrightness) {
        Slog.i(this.mTag, "Switching user newUserId=" + newUserId + " userSerial=" + userSerial + " newBrightness=" + newBrightness);
        this.mIsUserSwitching = true;
        if (this.mDpcExt.onSwitchUser(newUserId, this.mDisplayBrightnessController.getCurrentBrightness(), (this.mBrightnessReason.getModifier() & 1) == 1)) {
            this.mIsUserSwitching = false;
            return;
        }
        this.mIsUserSwitching = false;
        handleBrightnessModeChange();
        if (this.mBrightnessTracker != null) {
            this.mBrightnessTracker.onSwitchUser(newUserId);
        }
        if (this.mAutomaticBrightnessController != null) {
            this.mAutomaticBrightnessController.resetShortTermModel();
        }
        sendUpdatePowerState();
        this.mDpcExt.setDCMode();
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public ParceledListSlice<AmbientBrightnessDayStats> getAmbientBrightnessStats(int userId) {
        if (this.mBrightnessTracker == null) {
            return null;
        }
        return this.mBrightnessTracker.getAmbientBrightnessStats(userId);
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void persistBrightnessTrackerState() {
        if (this.mBrightnessTracker != null) {
            this.mBrightnessTracker.persistBrightnessTrackerState();
        }
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public boolean requestPowerState(DisplayManagerInternal.DisplayPowerRequest request, boolean waitForNegativeProximity) {
        synchronized (this.mLock) {
            if (this.mStopped) {
                return true;
            }
            boolean changed = this.mDisplayPowerProximityStateController.setPendingWaitForNegativeProximityLocked(waitForNegativeProximity);
            if (this.mPendingRequestLocked == null) {
                this.mPendingRequestLocked = new DisplayManagerInternal.DisplayPowerRequest(request);
                changed = true;
            } else if (!this.mPendingRequestLocked.equals(request)) {
                this.mPendingRequestLocked.copyFrom(request);
                changed = true;
            }
            if (this.mDpcExt.isUseProximityForceSuspendStateChanged(this.mDisplayId)) {
                this.mPendingRequestLocked.copyFrom(request);
                changed = true;
            }
            if (changed) {
                this.mDisplayReadyLocked = false;
                if (!this.mPendingRequestChangedLocked) {
                    this.mPendingRequestChangedLocked = true;
                    this.mDpcExt.updateBrightnessAnimationStatus(this.mPowerState, this.mPendingRequestLocked.policy, this.mLogicalDisplay, this.mDisplayId);
                    sendUpdatePowerStateLocked();
                }
            }
            this.mDpcExt.setPowerRequestPolicy(request.policy);
            if (changed) {
                Slog.d(this.mTag, "requestPowerState: " + request + ", waitForNegativeProximity=" + waitForNegativeProximity + " displayReady=" + this.mDisplayReadyLocked + " pendingRequest=" + this.mPendingRequestChangedLocked);
            }
            return this.mDisplayReadyLocked;
        }
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void overrideDozeScreenState(final int displayState, final int reason) {
        Slog.i(TAG, "New offload doze override: " + Display.stateToString(displayState));
        this.mHandler.postAtTime(new Runnable() { // from class: com.android.server.display.DisplayPowerController$$ExternalSyntheticLambda12
            @Override // java.lang.Runnable
            public final void run() {
                DisplayPowerController.this.lambda$overrideDozeScreenState$3(displayState, reason);
            }
        }, this.mClock.uptimeMillis());
    }

    /* JADX INFO: Access modifiers changed from: private */
    public /* synthetic */ void lambda$overrideDozeScreenState$3(int displayState, int reason) {
        if (this.mDisplayOffloadSession != null) {
            if (!DisplayManagerInternal.DisplayOffloadSession.isSupportedOffloadState(displayState) && displayState != 0) {
                return;
            }
            this.mDisplayStateController.overrideDozeScreenState(displayState, reason);
            lambda$new$0();
        }
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void setDisplayOffloadSession(DisplayManagerInternal.DisplayOffloadSession session) {
        if (session == this.mDisplayOffloadSession) {
            return;
        }
        unblockScreenOnByDisplayOffload();
        this.mDisplayOffloadSession = session;
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public BrightnessConfiguration getDefaultBrightnessConfiguration() {
        if (this.mAutomaticBrightnessController == null) {
            return null;
        }
        return this.mAutomaticBrightnessController.getDefaultConfig();
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void onDisplayChanged(final HighBrightnessModeMetadata hbmMetadata, int leadDisplayId) {
        this.mLeadDisplayId = leadDisplayId;
        final DisplayDevice device = this.mLogicalDisplay.getPrimaryDisplayDeviceLocked();
        if (device == null) {
            Slog.wtf(this.mTag, "Display Device is null in DisplayPowerController2 for display: " + this.mLogicalDisplay.getDisplayIdLocked());
            return;
        }
        final String uniqueId = device.getUniqueId();
        final DisplayDeviceConfig config = device.getDisplayDeviceConfig();
        final IBinder token = device.getDisplayTokenLocked();
        final DisplayDeviceInfo info = device.getDisplayDeviceInfoLocked();
        final boolean isEnabled = this.mLogicalDisplay.isEnabledLocked();
        final boolean isInTransition = this.mLogicalDisplay.isInTransitionLocked();
        final boolean isDisplayInternal = this.mLogicalDisplay.getPrimaryDisplayDeviceLocked() != null && this.mLogicalDisplay.getPrimaryDisplayDeviceLocked().getDisplayDeviceInfoLocked().type == 1;
        final String thermalBrightnessThrottlingDataId = this.mLogicalDisplay.getDisplayInfoLocked().thermalBrightnessThrottlingDataId;
        final int displayId = this.mLogicalDisplay.getDisplayIdLocked();
        final String powerThrottlingDataId = this.mLogicalDisplay.getPowerThrottlingDataIdLocked();
        this.mHandler.postAtTime(new Runnable() { // from class: com.android.server.display.DisplayPowerController$$ExternalSyntheticLambda3
            @Override // java.lang.Runnable
            public final void run() {
                DisplayPowerController.this.lambda$onDisplayChanged$4(isEnabled, isInTransition, device, uniqueId, config, thermalBrightnessThrottlingDataId, token, info, hbmMetadata, isDisplayInternal, powerThrottlingDataId, displayId);
            }
        }, this.mClock.uptimeMillis());
    }

    /* JADX INFO: Access modifiers changed from: private */
    public /* synthetic */ void lambda$onDisplayChanged$4(boolean isEnabled, boolean isInTransition, DisplayDevice device, String uniqueId, DisplayDeviceConfig config, String thermalBrightnessThrottlingDataId, IBinder token, DisplayDeviceInfo info, HighBrightnessModeMetadata hbmMetadata, boolean isDisplayInternal, String powerThrottlingDataId, int displayId) {
        boolean changed = false;
        if (this.mIsEnabled != isEnabled || this.mIsInTransition != isInTransition) {
            changed = true;
            this.mIsEnabled = isEnabled;
            this.mIsInTransition = isInTransition;
        }
        if (this.mDisplayDevice == device) {
            if (!Objects.equals(this.mThermalBrightnessThrottlingDataId, thermalBrightnessThrottlingDataId)) {
                changed = true;
                this.mThermalBrightnessThrottlingDataId = thermalBrightnessThrottlingDataId;
                this.mBrightnessThrottler.loadThermalBrightnessThrottlingDataFromDisplayDeviceConfig(config.getThermalBrightnessThrottlingDataMapByThrottlingId(), config.getTempSensor(), this.mThermalBrightnessThrottlingDataId, this.mUniqueDisplayId);
            }
        } else {
            changed = true;
            this.mDisplayDevice = device;
            this.mUniqueDisplayId = uniqueId;
            this.mDisplayStatsId = this.mUniqueDisplayId.hashCode();
            this.mDisplayDeviceConfig = config;
            this.mThermalBrightnessThrottlingDataId = thermalBrightnessThrottlingDataId;
            loadFromDisplayDeviceConfig(token, info, hbmMetadata);
            this.mDisplayPowerProximityStateController.notifyDisplayDeviceChanged(config);
            this.mPowerState.resetScreenState();
        }
        this.mIsDisplayInternal = isDisplayInternal;
        this.mBrightnessClamperController.onDisplayChanged(new BrightnessClamperController.DisplayDeviceData(uniqueId, thermalBrightnessThrottlingDataId, powerThrottlingDataId, config));
        if (changed || this.mWrapper.getLogicalDisplayMapper().isRemapDisabledSecondaryDisplayId(displayId)) {
            if (this.mDpcExt != null) {
                this.mIsPrimaryDisplay = this.mDpcExt.isPrimaryDisplay(this.mUniqueDisplayId);
                this.mDpcExt.setUniqueDisplayId(this.mIsPrimaryDisplay, this.mUniqueDisplayId);
            }
            if (this.mScreenBrightnessRampAnimator != null) {
                this.mScreenBrightnessRampAnimator.setDisplayId(displayId, this.mIsPrimaryDisplay);
            } else {
                Slog.e(this.mTag, "mScreenBrightnessRampAnimator is null, current dpc is " + this);
            }
            Slog.d(this.mTag, "onDisplayChanged id=" + displayId + " uniqueDisplayId=" + uniqueId + " enable=" + this.mLogicalDisplay.isEnabledLocked());
            lambda$new$0();
        }
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void stop() {
        synchronized (this.mLock) {
            clearDisplayBrightnessFollowersLocked();
            this.mStopped = true;
            Message msg = this.mHandler.obtainMessage(7);
            this.mHandler.sendMessageAtTime(msg, this.mClock.uptimeMillis());
            if (this.mAutomaticBrightnessController != null) {
                this.mAutomaticBrightnessController.stop();
            }
            this.mDisplayBrightnessController.stop();
            this.mContext.getContentResolver().unregisterContentObserver(this.mSettingsObserver);
        }
    }

    private void loadFromDisplayDeviceConfig(IBinder token, DisplayDeviceInfo info, HighBrightnessModeMetadata hbmMetadata) {
        loadBrightnessRampRates();
        loadNitsRange(this.mContext.getResources());
        setUpAutoBrightness(this.mContext, this.mHandler);
        reloadReduceBrightColours();
        setAnimatorRampSpeeds(false);
        this.mBrightnessRangeController.loadFromConfig(hbmMetadata, token, info, this.mDisplayDeviceConfig);
        this.mBrightnessThrottler.loadThermalBrightnessThrottlingDataFromDisplayDeviceConfig(this.mDisplayDeviceConfig.getThermalBrightnessThrottlingDataMapByThrottlingId(), this.mDisplayDeviceConfig.getTempSensor(), this.mThermalBrightnessThrottlingDataId, this.mUniqueDisplayId);
    }

    /* JADX INFO: Access modifiers changed from: private */
    public void sendUpdatePowerState() {
        synchronized (this.mLock) {
            sendUpdatePowerStateLocked();
        }
    }

    private void sendUpdatePowerStateLocked() {
        if (!this.mStopped && !this.mPendingUpdatePowerStateLocked) {
            this.mPendingUpdatePowerStateLocked = true;
            Message msg = this.mHandler.obtainMessage(1);
            this.mHandler.sendMessageAtTime(msg, this.mClock.uptimeMillis());
        }
    }

    private void initialize(int displayState) {
        this.mPowerState = this.mInjector.getDisplayPowerState(this.mBlanker, this.mColorFadeEnabled ? new ColorFade(this.mDisplayId) : null, this.mDisplayId, displayState, this.mDpcExt);
        if (this.mColorFadeEnabled) {
            this.mColorFadeOnAnimator = ObjectAnimator.ofFloat(this.mPowerState, DisplayPowerState.COLOR_FADE_LEVEL, 0.0f, 1.0f);
            this.mColorFadeOnAnimator.setDuration(250L);
            this.mColorFadeOnAnimator.addListener(this.mAnimatorListener);
            this.mColorFadeOffAnimator = ObjectAnimator.ofFloat(this.mPowerState, DisplayPowerState.COLOR_FADE_LEVEL, 1.0f, 0.0f);
            this.mColorFadeOffAnimator.setDuration(100L);
            this.mColorFadeOffAnimator.addListener(this.mAnimatorListener);
        }
        this.mScreenBrightnessRampAnimator = this.mInjector.getDualRampAnimator(this.mPowerState, DisplayPowerState.SCREEN_BRIGHTNESS_FLOAT, DisplayPowerState.SCREEN_SDR_BRIGHTNESS_FLOAT);
        this.mScreenBrightnessRampAnimator.setAnimationTimeLimits(this.mBrightnessRampIncreaseMaxTimeMillis, this.mBrightnessRampDecreaseMaxTimeMillis);
        this.mDpcExt.setPowerState(this.mPowerState);
        this.mScreenBrightnessRampAnimator.setDisplayId(this.mDisplayId, this.mIsPrimaryDisplay);
        Slog.d(this.mTag, "in initialize current dpc is " + this);
        this.mScreenBrightnessRampAnimator.setListener(this.mRampAnimatorListener);
        noteScreenState(this.mPowerState.getScreenState(), 1);
        noteScreenBrightness(this.mPowerState.getScreenBrightness());
        float brightness = this.mDisplayBrightnessController.convertToAdjustedNits(this.mPowerState.getScreenBrightness());
        if (this.mBrightnessTracker != null && brightness >= 0.0f) {
            this.mBrightnessTracker.start(brightness);
        }
        BrightnessSetting.BrightnessSettingListener brightnessSettingListener = new BrightnessSetting.BrightnessSettingListener() { // from class: com.android.server.display.DisplayPowerController$$ExternalSyntheticLambda1
            @Override // com.android.server.display.BrightnessSetting.BrightnessSettingListener
            public final void onBrightnessChanged(float f) {
                DisplayPowerController.this.lambda$initialize$5(f);
            }
        };
        this.mDisplayBrightnessController.registerBrightnessSettingChangeListener(brightnessSettingListener);
        this.mContext.getContentResolver().registerContentObserver(Settings.System.getUriFor("screen_auto_brightness_adj"), false, this.mSettingsObserver, -1);
        this.mContext.getContentResolver().registerContentObserver(Settings.Secure.getUriFor(GLOBAL_HBM_SELL_MODE), false, this.mSettingsObserver, -1);
        this.mContext.getContentResolver().registerContentObserver(Settings.System.getUriFor("screen_auto_brightness_adj_talkback"), false, this.mSettingsObserver, -1);
        this.mContext.getContentResolver().registerContentObserver(Settings.System.getUriFor("screen_brightness"), false, this.mSettingsObserver, -1);
        this.mContext.getContentResolver().registerContentObserver(Settings.System.getUriFor(SECOND_SCREEN_AUTO_BRIGHTNESS_ADJ), false, this.mSettingsObserver, -1);
        this.mContext.getContentResolver().registerContentObserver(Settings.System.getUriFor("screen_brightness_mode"), false, this.mSettingsObserver, -1);
        if (this.mFlags.areAutoBrightnessModesEnabled()) {
            this.mContext.getContentResolver().registerContentObserver(Settings.System.getUriFor("screen_brightness_for_als"), false, this.mSettingsObserver, -2);
        }
        handleBrightnessModeChange();
    }

    /* JADX INFO: Access modifiers changed from: private */
    public /* synthetic */ void lambda$initialize$5(float brightnessValue) {
        Message msg = this.mHandler.obtainMessage(8, Float.valueOf(brightnessValue));
        this.mHandler.sendMessageAtTime(msg, this.mClock.uptimeMillis());
    }

    /* JADX INFO: Access modifiers changed from: private */
    public void setUpAutoBrightness(Context context, Handler handler) {
        BrightnessMappingStrategy idleModeBrightnessMapper;
        this.mUseSoftwareAutoBrightnessConfig = this.mDisplayDeviceConfig.isAutoBrightnessAvailable() && (this.mDisplayId == 0 || this.mDpcExt.useSoftwareAutoBrightnessConfigInOtherDisplay(this.mDisplayId));
        if (!this.mUseSoftwareAutoBrightnessConfig && !this.mDpcExt.hasRemapDisable()) {
            return;
        }
        SparseArray<BrightnessMappingStrategy> brightnessMappers = new SparseArray<>();
        BrightnessMappingStrategy defaultModeBrightnessMapper = this.mInjector.getDefaultModeBrightnessMapper(context, this.mDisplayDeviceConfig, this.mDisplayWhiteBalanceController);
        brightnessMappers.append(0, defaultModeBrightnessMapper);
        boolean isIdleScreenBrightnessEnabled = context.getResources().getBoolean(17891702);
        if (isIdleScreenBrightnessEnabled && (idleModeBrightnessMapper = BrightnessMappingStrategy.create(context, this.mDisplayDeviceConfig, 1, this.mDisplayWhiteBalanceController)) != null) {
            brightnessMappers.append(1, idleModeBrightnessMapper);
        }
        BrightnessMappingStrategy dozeModeBrightnessMapper = BrightnessMappingStrategy.create(context, this.mDisplayDeviceConfig, 2, this.mDisplayWhiteBalanceController);
        if (this.mFlags.areAutoBrightnessModesEnabled() && dozeModeBrightnessMapper != null) {
            brightnessMappers.put(2, dozeModeBrightnessMapper);
        }
        if (this.mAutomaticBrightnessController != null) {
            this.mAutomaticBrightnessController.getUserLux();
            this.mAutomaticBrightnessController.getUserNits();
        }
        if (defaultModeBrightnessMapper == null) {
            this.mUseSoftwareAutoBrightnessConfig = false;
            return;
        }
        this.mDisplayDeviceConfig.getAmbientBrightnessHysteresis();
        this.mDisplayDeviceConfig.getScreenBrightnessHysteresis();
        this.mDisplayDeviceConfig.getAmbientBrightnessIdleHysteresis();
        this.mDisplayDeviceConfig.getScreenBrightnessIdleHysteresis();
        this.mDisplayDeviceConfig.getAutoBrightnessBrighteningLightDebounce();
        long darkeningLightDebounce = this.mDisplayDeviceConfig.getAutoBrightnessDarkeningLightDebounce();
        this.mDisplayDeviceConfig.getAutoBrightnessBrighteningLightDebounceIdle();
        this.mDisplayDeviceConfig.getAutoBrightnessDarkeningLightDebounceIdle();
        context.getResources().getBoolean(17891385);
        context.getResources().getInteger(17694890);
        int lightSensorRate = context.getResources().getInteger(17694751);
        int initialLightSensorRate = context.getResources().getInteger(17694750);
        if (initialLightSensorRate != -1) {
            if (initialLightSensorRate > lightSensorRate) {
                Slog.w(this.mTag, "Expected config_autoBrightnessInitialLightSensorRate (" + initialLightSensorRate + ") to be less than or equal to config_autoBrightnessLightSensorRate (" + lightSensorRate + ").");
            }
        }
        loadAmbientLightSensor();
        if (this.mBrightnessTracker != null && this.mDisplayId == 0) {
            this.mBrightnessTracker.setLightSensor(this.mLightSensor);
        }
        if (this.mAutomaticBrightnessController != null) {
            this.mAutomaticBrightnessController.stop();
        }
        this.mDpcExt.stop(this.mIsPrimaryDisplay);
        this.mAutomaticBrightnessController = this.mDpcExt.initAutomaticBrightnessController(this, handler.getLooper(), this.mSensorManager, this.mLightSensor, defaultModeBrightnessMapper, this.mDozeScaleFactor, lightSensorRate, darkeningLightDebounce);
        this.mDisplayBrightnessController.setUpAutoBrightness(this.mAutomaticBrightnessController, this.mSensorManager, this.mDisplayDeviceConfig, this.mHandler, defaultModeBrightnessMapper, this.mIsEnabled, this.mLeadDisplayId);
        this.mBrightnessEventRingBuffer = new RingBuffer<>(BrightnessEvent.class, 100);
        if (!this.mFlags.isRefactorDisplayPowerControllerEnabled()) {
            if (this.mScreenOffBrightnessSensorController != null) {
                this.mScreenOffBrightnessSensorController.stop();
                this.mScreenOffBrightnessSensorController = null;
            }
            loadScreenOffBrightnessSensor();
            int[] sensorValueToLux = this.mDisplayDeviceConfig.getScreenOffBrightnessSensorValueToLux();
            if (this.mScreenOffBrightnessSensor != null && sensorValueToLux != null) {
                this.mScreenOffBrightnessSensorController = this.mInjector.getScreenOffBrightnessSensorController(this.mSensorManager, this.mScreenOffBrightnessSensor, this.mHandler, new DisplayPowerController$$ExternalSyntheticLambda2(), sensorValueToLux, defaultModeBrightnessMapper);
            }
        }
    }

    private void loadBrightnessRampRates() {
        this.mBrightnessRampRateFastDecrease = this.mDisplayDeviceConfig.getBrightnessRampFastDecrease();
        this.mBrightnessRampRateFastIncrease = this.mDisplayDeviceConfig.getBrightnessRampFastIncrease();
        this.mBrightnessRampRateSlowDecrease = this.mDisplayDeviceConfig.getBrightnessRampSlowDecrease();
        this.mBrightnessRampRateSlowIncrease = this.mDisplayDeviceConfig.getBrightnessRampSlowIncrease();
        this.mBrightnessRampRateSlowDecreaseIdle = this.mDisplayDeviceConfig.getBrightnessRampSlowDecreaseIdle();
        this.mBrightnessRampRateSlowIncreaseIdle = this.mDisplayDeviceConfig.getBrightnessRampSlowIncreaseIdle();
        this.mBrightnessRampDecreaseMaxTimeMillis = this.mDisplayDeviceConfig.getBrightnessRampDecreaseMaxMillis();
        this.mBrightnessRampIncreaseMaxTimeMillis = this.mDisplayDeviceConfig.getBrightnessRampIncreaseMaxMillis();
        this.mBrightnessRampDecreaseMaxTimeIdleMillis = this.mDisplayDeviceConfig.getBrightnessRampDecreaseMaxIdleMillis();
        this.mBrightnessRampIncreaseMaxTimeIdleMillis = this.mDisplayDeviceConfig.getBrightnessRampIncreaseMaxIdleMillis();
    }

    private void loadNitsRange(Resources resources) {
        if (this.mDisplayDeviceConfig != null && this.mDisplayDeviceConfig.getNits() != null) {
            this.mNitsRange = this.mDisplayDeviceConfig.getNits();
            return;
        }
        Slog.w(this.mTag, "Screen brightness nits configuration is unavailable; falling back");
        this.mNitsRange = BrightnessMappingStrategy.getFloatArray(resources.obtainTypedArray(17236153));
    }

    private void reloadReduceBrightColours() {
        if (this.mCdsi != null && this.mCdsi.isReduceBrightColorsActivated()) {
            applyReduceBrightColorsSplineAdjustment();
        }
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void setAutomaticScreenBrightnessMode(int mode) {
        Message msg = this.mHandler.obtainMessage();
        msg.what = 14;
        msg.arg1 = mode;
        this.mHandler.sendMessageAtTime(msg, this.mClock.uptimeMillis());
    }

    private void setAnimatorRampSpeeds(boolean isIdle) {
        if (this.mScreenBrightnessRampAnimator == null) {
            return;
        }
        if (this.mFlags.isAdaptiveTone1Enabled() && isIdle) {
            this.mScreenBrightnessRampAnimator.setAnimationTimeLimits(this.mBrightnessRampIncreaseMaxTimeIdleMillis, this.mBrightnessRampDecreaseMaxTimeIdleMillis);
        } else {
            this.mScreenBrightnessRampAnimator.setAnimationTimeLimits(this.mBrightnessRampIncreaseMaxTimeMillis, this.mBrightnessRampDecreaseMaxTimeMillis);
        }
    }

    /* JADX INFO: Access modifiers changed from: private */
    public void cleanupHandlerThreadAfterStop() {
        float brightness;
        this.mDisplayPowerProximityStateController.cleanup();
        this.mBrightnessRangeController.stop();
        this.mBrightnessThrottler.stop();
        this.mBrightnessClamperController.stop();
        this.mHandler.removeCallbacksAndMessages(null);
        this.mWakelockController.releaseAll();
        if (this.mPowerState != null) {
            brightness = this.mPowerState.getScreenBrightness();
        } else {
            brightness = 0.0f;
        }
        reportStats(brightness);
        if (this.mPowerState != null) {
            this.mPowerState.stop();
            this.mPowerState = null;
        }
        if (!this.mFlags.isRefactorDisplayPowerControllerEnabled() && this.mScreenOffBrightnessSensorController != null) {
            this.mScreenOffBrightnessSensorController.stop();
        }
        if (this.mDisplayWhiteBalanceController != null) {
            this.mDisplayWhiteBalanceController.setEnabled(false);
        }
    }

    /* JADX INFO: Access modifiers changed from: private */
    /* renamed from: updatePowerState */
    public void lambda$new$0() {
        Trace.traceBegin(131072L, "DisplayPowerController#updatePowerState");
        updatePowerStateInternal();
        Trace.traceEnd(131072L);
    }

    /* JADX WARN: Code restructure failed: missing block: B:162:0x032f, code lost:
        if (r12.getDisplayBrightnessStrategyName().equals(com.android.server.display.brightness.strategy.DisplayBrightnessStrategyConstants.FALLBACK_BRIGHTNESS_STRATEGY_NAME) == false) goto L123;
     */
    /* JADX WARN: Removed duplicated region for block: B:137:0x02b2  */
    /* JADX WARN: Removed duplicated region for block: B:159:0x031d  */
    /* JADX WARN: Removed duplicated region for block: B:174:0x036d  */
    /* JADX WARN: Removed duplicated region for block: B:177:0x0377  */
    /* JADX WARN: Removed duplicated region for block: B:194:0x03c9  */
    /* JADX WARN: Removed duplicated region for block: B:208:0x03ff  */
    /* JADX WARN: Removed duplicated region for block: B:212:0x043d  */
    /* JADX WARN: Removed duplicated region for block: B:219:0x044d  */
    /* JADX WARN: Removed duplicated region for block: B:261:0x04d9  */
    /* JADX WARN: Removed duplicated region for block: B:263:0x04e8 A[ADDED_TO_REGION] */
    /* JADX WARN: Removed duplicated region for block: B:267:0x04f7 A[ADDED_TO_REGION] */
    /* JADX WARN: Removed duplicated region for block: B:280:0x05cf  */
    /* JADX WARN: Removed duplicated region for block: B:281:0x05d2  */
    /* JADX WARN: Removed duplicated region for block: B:284:0x05dd  */
    /* JADX WARN: Removed duplicated region for block: B:285:0x05e4  */
    /* JADX WARN: Removed duplicated region for block: B:288:0x0615  */
    /* JADX WARN: Removed duplicated region for block: B:294:0x0633  */
    /* JADX WARN: Removed duplicated region for block: B:295:0x0635  */
    /* JADX WARN: Removed duplicated region for block: B:298:0x0643 A[ADDED_TO_REGION] */
    /* JADX WARN: Removed duplicated region for block: B:316:0x06a3  */
    /* JADX WARN: Removed duplicated region for block: B:322:0x06c2  */
    /* JADX WARN: Removed duplicated region for block: B:325:0x06c7  */
    /* JADX WARN: Removed duplicated region for block: B:341:0x06f7  */
    /* JADX WARN: Removed duplicated region for block: B:347:0x0705  */
    /* JADX WARN: Removed duplicated region for block: B:358:0x0757  */
    /* JADX WARN: Removed duplicated region for block: B:370:0x07ab  */
    /* JADX WARN: Removed duplicated region for block: B:372:0x07b7  */
    /* JADX WARN: Removed duplicated region for block: B:375:0x07c1  */
    /* JADX WARN: Removed duplicated region for block: B:381:0x07f8 A[ADDED_TO_REGION] */
    /* JADX WARN: Removed duplicated region for block: B:395:0x0823  */
    /* JADX WARN: Removed duplicated region for block: B:398:0x082c  */
    /* JADX WARN: Removed duplicated region for block: B:399:0x082e  */
    /* JADX WARN: Removed duplicated region for block: B:402:0x0837  */
    /* JADX WARN: Removed duplicated region for block: B:403:0x0887  */
    /* JADX WARN: Removed duplicated region for block: B:406:0x088f  */
    /* JADX WARN: Removed duplicated region for block: B:420:0x0807 A[EXC_TOP_SPLITTER, SYNTHETIC] */
    /*
        Code decompiled incorrectly, please refer to instructions dump.
        To view partially-correct add '--show-bad-code' argument
    */
    private void updatePowerStateInternal() {
        /*
            Method dump skipped, instructions count: 2208
            To view this dump add '--comments-level debug' option
        */
        throw new UnsupportedOperationException("Method not decompiled: com.android.server.display.DisplayPowerController.updatePowerStateInternal():void");
    }

    /* JADX INFO: Access modifiers changed from: private */
    public void setDwbcOverride(float cct) {
        if (this.mDisplayWhiteBalanceController != null) {
            this.mDisplayWhiteBalanceController.setAmbientColorTemperatureOverride(cct);
            lambda$new$0();
        }
    }

    /* JADX INFO: Access modifiers changed from: private */
    public void setDwbcStrongMode(int arg) {
        if (this.mDisplayWhiteBalanceController != null) {
            boolean isIdle = arg == 1;
            this.mDisplayWhiteBalanceController.setStrongModeEnabled(isIdle);
        }
    }

    /* JADX INFO: Access modifiers changed from: private */
    public void setDwbcLoggingEnabled(int arg) {
        if (this.mDisplayWhiteBalanceController != null) {
            boolean enabled = arg == 1;
            this.mDisplayWhiteBalanceController.setLoggingEnabled(enabled);
            this.mDisplayWhiteBalanceSettings.setLoggingEnabled(enabled);
        }
    }

    public void updateFpsWhenDcChange(boolean enter) {
        if (this.mDCBrightnessChange == enter) {
            return;
        }
        Slog.d(this.mTag, "debug enter: " + enter);
        this.mDpcExt.updateFpsWhenDcChange(enter);
        this.mDCBrightnessChange = enter;
    }

    public void updateFpsIfNeeded(float brightness) {
        boolean tmpMode = this.mDpcExt.isDCMode() && brightness < ((float) DC_MODE_BRIGHT_EDGE) && this.mScreenBrightnessRampAnimator.isAnimating();
        if (DC_MODE_BRIGHT_CUSTOMIZATION) {
            if (this.mUpdateFpsForDc != tmpMode) {
                if (tmpMode) {
                    this.mResetFpsStatePending = false;
                    updateFpsWhenDcChange(true);
                } else {
                    this.mResetFpsStatePending = true;
                    this.mHandler.removeMessages(21);
                    Message msg = this.mHandler.obtainMessage(21);
                    this.mHandler.sendMessageDelayed(msg, 1000L);
                }
            }
            this.mUpdateFpsForDc = tmpMode;
        }
    }

    @Override // com.android.server.display.AutomaticBrightnessController.Callbacks
    public void updateBrightness() {
        sendUpdatePowerState();
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void ignoreProximitySensorUntilChanged() {
        this.mDisplayPowerProximityStateController.ignoreProximitySensorUntilChanged();
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void setBrightnessConfiguration(BrightnessConfiguration c, boolean shouldResetShortTermModel) {
        Message msg = this.mHandler.obtainMessage(4, shouldResetShortTermModel ? 1 : 0, 0, c);
        msg.sendToTarget();
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void setTemporaryBrightness(float brightness) {
        Message msg = this.mHandler.obtainMessage(5, Float.floatToIntBits(brightness), 0);
        msg.sendToTarget();
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void setTemporaryAutoBrightnessAdjustment(float adjustment) {
        this.mDpcExt.setTemporaryAutoBrightnessAdjustment(adjustment);
        if (adjustment == this.mDpcExt.getAdjustmentGalleryIn() || adjustment == this.mDpcExt.getAdjustmentGalleryOut()) {
            if (!this.mDpcExt.isGalleryBrightnessEnhanceSupport()) {
                return;
            }
            Slog.d(this.mTag, "setTemporaryAutoBrightnessAdjustment=" + adjustment);
        }
        Message msg = this.mHandler.obtainMessage(6, Float.floatToIntBits(adjustment), 0);
        msg.sendToTarget();
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void setBrightnessFromOffload(float brightness) {
        Message msg = this.mHandler.obtainMessage(17, Float.floatToIntBits(brightness), 0);
        this.mHandler.sendMessageAtTime(msg, this.mClock.uptimeMillis());
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public float[] getAutoBrightnessLevels(int mode) {
        int preset = Settings.System.getIntForUser(this.mContext.getContentResolver(), "screen_brightness_for_als", 2, -2);
        return this.mDisplayDeviceConfig.getAutoBrightnessBrighteningLevels(mode, preset);
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public float[] getAutoBrightnessLuxLevels(int mode) {
        int preset = Settings.System.getIntForUser(this.mContext.getContentResolver(), "screen_brightness_for_als", 2, -2);
        return this.mDisplayDeviceConfig.getAutoBrightnessBrighteningLevelsLux(mode, preset);
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public BrightnessInfo getBrightnessInfo() {
        BrightnessInfo brightnessInfo;
        synchronized (this.mCachedBrightnessInfo) {
            brightnessInfo = new BrightnessInfo(this.mCachedBrightnessInfo.brightness.value, this.mCachedBrightnessInfo.adjustedBrightness.value, this.mCachedBrightnessInfo.brightnessMin.value, this.mCachedBrightnessInfo.brightnessMax.value, this.mCachedBrightnessInfo.hbmMode.value, this.mCachedBrightnessInfo.hbmTransitionPoint.value, this.mCachedBrightnessInfo.brightnessMaxReason.value);
        }
        return brightnessInfo;
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void onBootCompleted() {
        Message msg = this.mHandler.obtainMessage(13);
        this.mHandler.sendMessageAtTime(msg, this.mClock.uptimeMillis());
    }

    private boolean saveBrightnessInfo(float brightness) {
        return saveBrightnessInfo(brightness, null);
    }

    private boolean saveBrightnessInfo(float brightness, DisplayBrightnessState state) {
        return saveBrightnessInfo(brightness, brightness, state);
    }

    private boolean saveBrightnessInfo(float brightness, float adjustedBrightness, DisplayBrightnessState state) {
        boolean changed;
        synchronized (this.mCachedBrightnessInfo) {
            float stateMax = state != null ? state.getMaxBrightness() : this.mScreenBrightnessNormalMaximum;
            float stateMin = state != null ? state.getMinBrightness() : this.mScreenBrightnessRangeMinimum;
            float minBrightness = Math.max(stateMin, Math.min(this.mScreenBrightnessRangeMinimum, stateMax));
            float maxBrightness = Math.min(this.mScreenBrightnessNormalMaximum, stateMax);
            boolean changed2 = false | this.mCachedBrightnessInfo.checkAndSetFloat(this.mCachedBrightnessInfo.brightness, brightness);
            changed = changed2 | this.mCachedBrightnessInfo.checkAndSetFloat(this.mCachedBrightnessInfo.adjustedBrightness, adjustedBrightness) | this.mCachedBrightnessInfo.checkAndSetFloat(this.mCachedBrightnessInfo.brightnessMin, minBrightness) | this.mCachedBrightnessInfo.checkAndSetFloat(this.mCachedBrightnessInfo.brightnessMax, maxBrightness) | this.mCachedBrightnessInfo.checkAndSetInt(this.mCachedBrightnessInfo.hbmMode, this.mBrightnessRangeController.getHighBrightnessMode()) | this.mCachedBrightnessInfo.checkAndSetFloat(this.mCachedBrightnessInfo.hbmTransitionPoint, this.mBrightnessRangeController.getTransitionPoint()) | this.mCachedBrightnessInfo.checkAndSetInt(this.mCachedBrightnessInfo.brightnessMaxReason, this.mBrightnessClamperController.getBrightnessMaxReason());
        }
        return changed;
    }

    /* JADX INFO: Access modifiers changed from: package-private */
    /* renamed from: postBrightnessChangeRunnable */
    public void lambda$new$2() {
        if (!this.mHandler.hasCallbacks(this.mOnBrightnessChangeRunnable)) {
            this.mHandler.post(this.mOnBrightnessChangeRunnable);
        }
    }

    private HighBrightnessModeController createHbmControllerLocked(HighBrightnessModeMetadata hbmMetadata, Runnable modeChangeCallback) {
        DisplayDeviceConfig ddConfig = this.mDisplayDevice.getDisplayDeviceConfig();
        IBinder displayToken = this.mDisplayDevice.getDisplayTokenLocked();
        String displayUniqueId = this.mDisplayDevice.getUniqueId();
        DisplayDeviceConfig.HighBrightnessModeData hbmData = ddConfig != null ? ddConfig.getHighBrightnessModeData() : null;
        DisplayDeviceInfo info = this.mDisplayDevice.getDisplayDeviceInfoLocked();
        return this.mInjector.getHighBrightnessModeController(this.mHandler, info.width, info.height, displayToken, displayUniqueId, 0.0f, 1.0f, hbmData, new HighBrightnessModeController.HdrBrightnessDeviceConfig() { // from class: com.android.server.display.DisplayPowerController$$ExternalSyntheticLambda4
            @Override // com.android.server.display.HighBrightnessModeController.HdrBrightnessDeviceConfig
            public final float getHdrBrightnessFromSdr(float f, float f2) {
                float lambda$createHbmControllerLocked$6;
                lambda$createHbmControllerLocked$6 = DisplayPowerController.this.lambda$createHbmControllerLocked$6(f, f2);
                return lambda$createHbmControllerLocked$6;
            }
        }, modeChangeCallback, hbmMetadata, this.mContext);
    }

    /* JADX INFO: Access modifiers changed from: private */
    public /* synthetic */ float lambda$createHbmControllerLocked$6(float sdrBrightness, float maxDesiredHdrSdrRatio) {
        return this.mDisplayDeviceConfig.getHdrBrightnessFromSdr(sdrBrightness, maxDesiredHdrSdrRatio);
    }

    private BrightnessThrottler createBrightnessThrottlerLocked() {
        DisplayDevice device = this.mLogicalDisplay.getPrimaryDisplayDeviceLocked();
        DisplayDeviceConfig ddConfig = device.getDisplayDeviceConfig();
        return new BrightnessThrottler(this.mHandler, new Runnable() { // from class: com.android.server.display.DisplayPowerController$$ExternalSyntheticLambda11
            @Override // java.lang.Runnable
            public final void run() {
                DisplayPowerController.this.lambda$createBrightnessThrottlerLocked$7();
            }
        }, this.mUniqueDisplayId, this.mLogicalDisplay.getDisplayInfoLocked().thermalBrightnessThrottlingDataId, ddConfig);
    }

    /* JADX INFO: Access modifiers changed from: private */
    public /* synthetic */ void lambda$createBrightnessThrottlerLocked$7() {
        sendUpdatePowerState();
        lambda$new$2();
    }

    private void blockScreenOn() {
        this.mDpcExt.removeMessageWhenScreenOn(this.mHandler, 2);
        if (this.mPendingScreenOnUnblocker == null) {
            Trace.asyncTraceBegin(131072L, SCREEN_ON_BLOCKED_TRACE_NAME, 0);
            this.mPendingScreenOnUnblocker = new ScreenOnUnblocker();
            this.mScreenOnBlockStartRealTime = SystemClock.elapsedRealtime();
            Slog.i(this.mTag, "Blocking screen on until initial contents have been drawn.");
        }
    }

    /* JADX INFO: Access modifiers changed from: private */
    public void unblockScreenOn() {
        this.mDpcExt.removeMessageWhenScreenOn(this.mHandler, 2);
        if (this.mPendingScreenOnUnblocker != null) {
            this.mPendingScreenOnUnblocker = null;
            long delay = SystemClock.elapsedRealtime() - this.mScreenOnBlockStartRealTime;
            Slog.i(this.mTag, "Unblocked screen on after " + delay + " ms");
            Trace.asyncTraceEnd(131072L, SCREEN_ON_BLOCKED_TRACE_NAME, 0);
        }
    }

    private void blockScreenOff() {
        if (this.mPendingScreenOffUnblocker == null) {
            Trace.asyncTraceBegin(131072L, SCREEN_OFF_BLOCKED_TRACE_NAME, 0);
            this.mPendingScreenOffUnblocker = new ScreenOffUnblocker();
            this.mScreenOffBlockStartRealTime = SystemClock.elapsedRealtime();
            Slog.i(this.mTag, "Blocking screen off");
        }
    }

    /* JADX INFO: Access modifiers changed from: private */
    public void unblockScreenOff() {
        if (this.mPendingScreenOffUnblocker != null) {
            this.mPendingScreenOffUnblocker = null;
            long delay = SystemClock.elapsedRealtime() - this.mScreenOffBlockStartRealTime;
            Slog.i(this.mTag, "Unblocked screen off after " + delay + " ms");
            this.mBrightnessTracker.screenOffAction();
            Trace.asyncTraceEnd(131072L, SCREEN_OFF_BLOCKED_TRACE_NAME, 0);
        }
    }

    private void blockScreenOnByDisplayOffload(final DisplayManagerInternal.DisplayOffloadSession displayOffloadSession) {
        if (this.mPendingScreenOnUnblockerByDisplayOffload != null || displayOffloadSession == null) {
            return;
        }
        this.mScreenTurningOnWasBlockedByDisplayOffload = true;
        Trace.asyncTraceBegin(131072L, SCREEN_ON_BLOCKED_BY_DISPLAYOFFLOAD_TRACE_NAME, 0);
        this.mScreenOnBlockByDisplayOffloadStartRealTime = SystemClock.elapsedRealtime();
        this.mPendingScreenOnUnblockerByDisplayOffload = new Runnable() { // from class: com.android.server.display.DisplayPowerController$$ExternalSyntheticLambda13
            @Override // java.lang.Runnable
            public final void run() {
                DisplayPowerController.this.lambda$blockScreenOnByDisplayOffload$8(displayOffloadSession);
            }
        };
        if (!displayOffloadSession.blockScreenOn(this.mPendingScreenOnUnblockerByDisplayOffload)) {
            this.mPendingScreenOnUnblockerByDisplayOffload = null;
            long delay = SystemClock.elapsedRealtime() - this.mScreenOnBlockByDisplayOffloadStartRealTime;
            Slog.w(this.mTag, "Tried blocking screen on for offloading but failed. So, end trace after " + delay + " ms.");
            Trace.asyncTraceEnd(131072L, SCREEN_ON_BLOCKED_BY_DISPLAYOFFLOAD_TRACE_NAME, 0);
            return;
        }
        Slog.i(this.mTag, "Blocking screen on for offloading.");
    }

    /* JADX INFO: Access modifiers changed from: private */
    /* renamed from: onDisplayOffloadUnblockScreenOn */
    public void lambda$blockScreenOnByDisplayOffload$8(DisplayManagerInternal.DisplayOffloadSession displayOffloadSession) {
        Message msg = this.mHandler.obtainMessage(18, displayOffloadSession);
        this.mHandler.sendMessage(msg);
    }

    /* JADX INFO: Access modifiers changed from: private */
    public void unblockScreenOnByDisplayOffload() {
        if (this.mPendingScreenOnUnblockerByDisplayOffload == null) {
            return;
        }
        this.mPendingScreenOnUnblockerByDisplayOffload = null;
        long delay = SystemClock.elapsedRealtime() - this.mScreenOnBlockByDisplayOffloadStartRealTime;
        Slog.i(this.mTag, "Unblocked screen on for offloading after " + delay + " ms");
        Trace.asyncTraceEnd(131072L, SCREEN_ON_BLOCKED_BY_DISPLAYOFFLOAD_TRACE_NAME, 0);
    }

    private boolean setScreenState(int state, int reason) {
        return setScreenState(state, reason, false);
    }

    private boolean setScreenState(int state, int reason, boolean reportOnly) {
        boolean isOff = state == 1 || state == 3 || state == 4;
        boolean isOn = state == 2;
        boolean changed = this.mPowerState.getScreenState() != state;
        if (isOn && changed && !this.mScreenTurningOnWasBlockedByDisplayOffload) {
            blockScreenOnByDisplayOffload(this.mDisplayOffloadSession);
        } else if (!isOn && this.mScreenTurningOnWasBlockedByDisplayOffload) {
            unblockScreenOnByDisplayOffload();
            this.mScreenTurningOnWasBlockedByDisplayOffload = false;
        }
        if (changed || this.mReportedScreenStateToPolicy == -1) {
            if (isOff && !this.mDisplayPowerProximityStateController.isScreenOffBecauseOfProximity()) {
                if (this.mReportedScreenStateToPolicy == 2 || this.mReportedScreenStateToPolicy == -1) {
                    setReportedScreenState(3);
                    blockScreenOff();
                    this.mWindowManagerPolicy.screenTurningOff(this.mDisplayId, this.mPendingScreenOffUnblocker);
                    unblockScreenOff();
                } else if (this.mPendingScreenOffUnblocker != null) {
                    return false;
                }
            }
            this.mDpcExt.setScreenStateExt(this.mIsPrimaryDisplay, state, this.mPowerState, this.mPowerRequest);
            if (!reportOnly && changed && readyToUpdateDisplayState() && this.mPendingScreenOffUnblocker == null && this.mPendingScreenOnUnblockerByDisplayOffload == null) {
                Trace.traceCounter(131072L, "ScreenState", state);
                String propertyValue = String.valueOf(state);
                try {
                    SystemProperties.set("debug.tracing.screen_state", propertyValue);
                } catch (RuntimeException e) {
                    Slog.e(this.mTag, "Failed to set a system property: key=debug.tracing.screen_state value=" + propertyValue + " " + e.getMessage());
                }
                this.mDpcExt.handlePwkMonitorForTheia(state, isOff);
                this.mPowerState.setScreenState(state, reason);
                noteScreenState(state, reason);
            }
        }
        if (DEBUG_PANIC) {
            Slog.d(this.mTag, "setScreenState: isOff=" + isOff + ", mReportedScreenStateToPolicy=" + this.mReportedScreenStateToPolicy);
        }
        if (isOff && this.mReportedScreenStateToPolicy != 0 && !this.mDisplayPowerProximityStateController.isScreenOffBecauseOfProximity()) {
            setReportedScreenState(0);
            unblockScreenOn();
            this.mDpcExt.unblockDisplayReady();
            this.mWindowManagerPolicy.screenTurnedOff(this.mDisplayId, this.mIsInTransition);
        } else if (!isOff && this.mReportedScreenStateToPolicy == 3) {
            unblockScreenOff();
            this.mWindowManagerPolicy.screenTurnedOff(this.mDisplayId, this.mIsInTransition);
            setReportedScreenState(0);
        }
        if (!isOff && (this.mReportedScreenStateToPolicy == 0 || this.mReportedScreenStateToPolicy == -1)) {
            setReportedScreenState(1);
            if (DEBUG) {
                Slog.d(this.mTag, "setScreenState: ColorFadeLevel=" + this.mPowerState.getColorFadeLevel());
            }
            if (this.mPowerState.getColorFadeLevel() == 0.0f) {
                blockScreenOn();
            } else {
                unblockScreenOn();
            }
            this.mWindowManagerPolicy.screenTurningOn(this.mDisplayId, this.mPendingScreenOnUnblocker);
        }
        if (this.isRM) {
            resetCabc(state);
        }
        if (this.mPendingScreenOnUnblocker == null && this.mPendingScreenOnUnblockerByDisplayOffload == null) {
            return !this.mDpcExt.isBlockScreenOnByBiometrics() || "optical".equals(FP_SENSOR_TYPE) || "ultrasonic".equals(FP_SENSOR_TYPE);
        }
        return false;
    }

    private void resetCabc(int state) {
        if (this.mLastState != state && state == 2 && this.mReportedScreenStateToPolicy == 2 && getScreenBrightnessSetting() > 0.0f) {
            this.mLastState = state;
            Message msg = this.mHandler.obtainMessage(22);
            this.mHandler.sendMessageDelayed(msg, 100L);
        }
        if (state != 2) {
            this.mLastState = state;
        }
    }

    private void setReportedScreenState(int state) {
        Trace.traceCounter(131072L, "ReportedScreenStateToPolicy", state);
        this.mReportedScreenStateToPolicy = state;
        if (state == 2) {
            this.mScreenTurningOnWasBlockedByDisplayOffload = false;
        }
    }

    private void loadAmbientLightSensor() {
        int fallbackType = this.mDisplayId == 0 ? 5 : 0;
        this.mLightSensor = SensorUtils.findSensor(this.mSensorManager, this.mDisplayDeviceConfig.getAmbientLightSensor(), fallbackType);
    }

    private void loadScreenOffBrightnessSensor() {
        this.mScreenOffBrightnessSensor = SensorUtils.findSensor(this.mSensorManager, this.mDisplayDeviceConfig.getScreenOffBrightnessSensor(), 0);
    }

    private float clampScreenBrightness(float value) {
        if (this.mDpcExt.hasRemapDisable()) {
            return MathUtils.constrain(value, this.mScreenBrightnessRangeMinimum, this.mScreenBrightnessRangeMaximum);
        }
        return value;
    }

    /* JADX INFO: Access modifiers changed from: private */
    public void animateScreenBrightness(float target, float sdrTarget, float rate) {
        animateScreenBrightness(target, sdrTarget, rate, false);
    }

    private void animateScreenBrightness(float target, float sdrTarget, float rate, boolean ignoreAnimationLimits) {
        this.mDpcExt.animateScreenBrightness(this.mScreenBrightnessRampAnimator, target, sdrTarget, rate, this.mPowerRequest, this.mPowerState);
        if (this.mScreenBrightnessRampAnimator.animateTo(target, sdrTarget, rate, ignoreAnimationLimits)) {
            Trace.traceCounter(131072L, "TargetScreenBrightness", (int) target);
            String propertyValue = String.valueOf(target);
            try {
                SystemProperties.set("debug.tracing.screen_brightness", propertyValue);
            } catch (RuntimeException e) {
                Slog.e(this.mTag, "Failed to set a system property: key=debug.tracing.screen_brightness value=" + propertyValue + " " + e.getMessage());
            }
            noteScreenBrightness(target);
        }
    }

    private void animateScreenStateChange(int target, int reason, boolean performScreenOffTransition) {
        if (DisplayDisable && target == 2 && this.mDpcExt.isBlockedBySideFingerprint()) {
            Slog.d(this.mTag, "animateScreenStateChange state:" + Display.stateToString(target));
            return;
        }
        if (this.mColorFadeEnabled && (this.mColorFadeOnAnimator.isStarted() || this.mColorFadeOffAnimator.isStarted())) {
            if (target != 2) {
                Slog.d(this.mTag, "animateScreenStateChange animation in progress state:" + Display.stateToString(target));
                return;
            }
            this.mPendingScreenOff = false;
            if (this.mColorFadeOffAnimator.isStarted()) {
                this.mColorFadeOffAnimator.cancel();
            }
        }
        if (this.mDisplayBlanksAfterDozeConfig && Display.isDozeState(this.mPowerState.getScreenState()) && !Display.isDozeState(target) && target != 2) {
            this.mPowerState.prepareColorFade(this.mContext, this.mColorFadeFadesConfig ? 2 : 0);
            if (this.mColorFadeOffAnimator != null) {
                this.mColorFadeOffAnimator.end();
            }
            setScreenState(1, reason, target != 1);
        }
        if (this.mPendingScreenOff && target != 1) {
            setScreenState(1, reason);
            this.mPendingScreenOff = false;
            this.mPowerState.dismissColorFadeResources();
        }
        if (target == 2) {
            if (Display.isDozeState(this.mPowerState.getScreenState()) && this.mPowerState.getColorFadeLevel() == 0.0f) {
                this.mPowerState.setColorFadeLevel(1.0f);
                this.mPowerState.dismissColorFade();
                Slog.d(this.mTag, "animateScreenStateChange target == Display.STATE_ON, current is doze");
            }
            if (!setScreenState(2, reason)) {
                Slog.d(this.mTag, "animateScreenStateChange screen on blocked blocker=" + this.mPendingScreenOnUnblocker);
                return;
            }
            this.mPowerState.setColorFadeLevel(1.0f);
            this.mPowerState.dismissColorFade();
        } else if (target == 3) {
            if (this.mScreenBrightnessRampAnimator.isAnimating() && this.mPowerState.getScreenState() == 2) {
                Slog.d(this.mTag, "animateScreenStateChange DOZE isAnimating");
            } else if (!setScreenState(3, reason)) {
                Slog.d(this.mTag, "animateScreenStateChange DOZE setScreenState");
            } else {
                this.mPowerState.setColorFadeLevel(1.0f);
                this.mPowerState.dismissColorFade();
            }
        } else if (target == 4) {
            if (!this.mScreenBrightnessRampAnimator.isAnimating() || this.mPowerState.getScreenState() == 4) {
                if (this.mPowerState.getScreenState() != 4) {
                    setScreenState(4, reason);
                }
                this.mPowerState.setColorFadeLevel(1.0f);
                this.mPowerState.dismissColorFade();
                return;
            }
            Slog.d(this.mTag, "animateScreenStateChange DOZE_SUSPEND isAnimating");
        } else if (target == 6) {
            if (!this.mScreenBrightnessRampAnimator.isAnimating() || this.mPowerState.getScreenState() == 6) {
                if (this.mPowerState.getScreenState() != 6) {
                    if (!setScreenState(2, reason)) {
                        return;
                    }
                    setScreenState(6, reason);
                }
                this.mPowerState.setColorFadeLevel(1.0f);
                this.mPowerState.dismissColorFade();
            }
        } else {
            this.mPendingScreenOff = true;
            boolean isFolding = this.mDpcExt.isFolding();
            if (DEBUG) {
                Slog.d(this.mTag, "isFolding = " + isFolding);
            }
            if (!this.mColorFadeEnabled || isFolding) {
                this.mPowerState.setColorFadeLevel(0.0f);
            }
            if (this.mDpcExt.isSilentRebootFirstGoToSleep(this.mDisplayId)) {
                this.mPowerState.setColorFadeLevel(0.0f);
            }
            if (this.mPowerState.getColorFadeLevel() == 0.0f) {
                setScreenState(1, reason);
                this.mPendingScreenOff = false;
                this.mPowerState.dismissColorFadeResources();
                return;
            }
            if (performScreenOffTransition) {
                if (this.mPowerState.prepareColorFade(this.mContext, this.mColorFadeFadesConfig ? 2 : 1) && this.mPowerState.getScreenState() != 1) {
                    this.mColorFadeOffAnimator.start();
                    return;
                }
            }
            this.mColorFadeOffAnimator.end();
        }
    }

    private void sendOnStateChangedWithWakelock() {
        boolean wakeLockAcquired = this.mWakelockController.acquireWakelock(4);
        if (wakeLockAcquired) {
            this.mHandler.post(this.mWakelockController.getOnStateChangedRunnable());
        }
    }

    private void logDisplayPolicyChanged(int newPolicy) {
        LogMaker log = new LogMaker(1696);
        log.setType(6);
        log.setSubtype(newPolicy);
        MetricsLogger.action(log);
    }

    /* JADX INFO: Access modifiers changed from: private */
    public void handleSettingsChange() {
        this.mDisplayBrightnessController.setPendingScreenBrightness(getScreenBrightnessSetting());
        this.mDpcExt.setGlobalHbmSellMode();
        this.mAutomaticBrightnessStrategy.updatePendingAutoBrightnessAdjustments();
        sendUpdatePowerState();
    }

    /* JADX INFO: Access modifiers changed from: private */
    public void handleBrightnessModeChange() {
        int screenBrightnessModeSetting = Settings.System.getIntForUser(this.mContext.getContentResolver(), "screen_brightness_mode", 0, -2);
        this.mAutomaticBrightnessStrategy.setUseAutoBrightness(screenBrightnessModeSetting == 1);
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public float getScreenBrightnessSetting() {
        float brightness;
        this.mDisplayBrightnessController.getScreenBrightnessSetting();
        String v = Settings.System.getStringForUser(this.mContext.getContentResolver(), "screen_brightness", -2);
        try {
            brightness = v != null ? Integer.parseInt(v) : this.mScreenBrightnessDefault;
        } catch (NumberFormatException e) {
            brightness = this.mScreenBrightnessDefault;
        }
        if (!this.mIsUserSwitching) {
            return this.mDpcExt.handleScreenBrightnessSettingChange(brightness);
        }
        return brightness;
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public float getDozeBrightnessForOffload() {
        return this.mDisplayBrightnessController.getCurrentBrightness() * this.mDozeScaleFactor;
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void setBrightness(float brightness) {
        this.mDisplayBrightnessController.setBrightness(clampScreenBrightness(brightness), this.mBrightnessRangeController.getCurrentBrightnessMax());
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void setBrightness(float brightness, int userSerial) {
        this.mDisplayBrightnessController.setBrightness(clampScreenBrightness(brightness), userSerial, this.mBrightnessRangeController.getCurrentBrightnessMax());
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public int getDisplayId() {
        return this.mDisplayId;
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public int getLeadDisplayId() {
        return this.mLeadDisplayId;
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void setBrightnessToFollow(float leadDisplayBrightness, float nits, float ambientLux, boolean slowChange) {
        this.mBrightnessRangeController.onAmbientLuxChange(ambientLux);
        if (nits == -1.0f) {
            this.mDisplayBrightnessController.setBrightnessToFollow(leadDisplayBrightness, slowChange);
        } else {
            float brightness = this.mDisplayBrightnessController.getBrightnessFromNits(nits);
            if (BrightnessUtils.isValidBrightnessValue(brightness, this.mScreenBrightnessRangeMinimum, this.mScreenBrightnessRangeMaximum)) {
                this.mDisplayBrightnessController.setBrightnessToFollow(brightness, slowChange);
            } else {
                this.mDisplayBrightnessController.setBrightnessToFollow(leadDisplayBrightness, slowChange);
            }
        }
        sendUpdatePowerState();
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void addDisplayBrightnessFollower(DisplayPowerControllerInterface follower) {
        synchronized (this.mLock) {
            this.mDisplayBrightnessFollowers.append(follower.getDisplayId(), follower);
            sendUpdatePowerStateLocked();
        }
    }

    private float getBrightnessByNit(float nit) {
        return this.mDpcExt.getBrightnessByNit(nit);
    }

    private float getNitByBrightness(float brightness) {
        return this.mDpcExt.getNitByBrightness(brightness);
    }

    private float convertToAdjustedNits(float brightness) {
        return this.mDpcExt.getNitByBrightness(brightness);
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void removeDisplayBrightnessFollower(final DisplayPowerControllerInterface follower) {
        synchronized (this.mLock) {
            this.mDisplayBrightnessFollowers.remove(follower.getDisplayId());
            this.mHandler.postAtTime(new Runnable() { // from class: com.android.server.display.DisplayPowerController$$ExternalSyntheticLambda6
                @Override // java.lang.Runnable
                public final void run() {
                    DisplayPowerControllerInterface.this.setBrightnessToFollow(Float.NaN, -1.0f, 0.0f, false);
                }
            }, this.mClock.uptimeMillis());
        }
    }

    private void clearDisplayBrightnessFollowersLocked() {
        for (int i = 0; i < this.mDisplayBrightnessFollowers.size(); i++) {
            final DisplayPowerControllerInterface follower = this.mDisplayBrightnessFollowers.valueAt(i);
            this.mHandler.postAtTime(new Runnable() { // from class: com.android.server.display.DisplayPowerController$$ExternalSyntheticLambda0
                @Override // java.lang.Runnable
                public final void run() {
                    DisplayPowerControllerInterface.this.setBrightnessToFollow(Float.NaN, -1.0f, 0.0f, false);
                }
            }, this.mClock.uptimeMillis());
        }
        this.mDisplayBrightnessFollowers.clear();
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void dump(final PrintWriter pw) {
        synchronized (this.mLock) {
            pw.println();
            pw.println("Display Power Controller:");
            pw.println("  mDisplayId=" + this.mDisplayId);
            pw.println("  mLeadDisplayId=" + this.mLeadDisplayId);
            pw.println("  mLightSensor=" + this.mLightSensor);
            pw.println("  mDisplayBrightnessFollowers=" + this.mDisplayBrightnessFollowers);
            pw.println();
            pw.println("Display Power Controller Locked State:");
            pw.println("  mDisplayReadyLocked=" + this.mDisplayReadyLocked);
            pw.println("  mPendingRequestLocked=" + this.mPendingRequestLocked);
            pw.println("  mPendingRequestChangedLocked=" + this.mPendingRequestChangedLocked);
            pw.println("  mPendingUpdatePowerStateLocked=" + this.mPendingUpdatePowerStateLocked);
        }
        pw.println();
        pw.println("Display Power Controller Configuration:");
        pw.println("  mScreenBrightnessDozeConfig=" + this.mScreenBrightnessDozeConfig);
        pw.println("  mScreenBrightnessRangeMinimum=" + this.mScreenBrightnessRangeMinimum);
        pw.println("  mScreenBrightnessRangeMaximum=" + this.mScreenBrightnessRangeMaximum);
        pw.println("  mScreenBrightnessNormalMaximum=" + this.mScreenBrightnessNormalMaximum);
        pw.println("  mUseSoftwareAutoBrightnessConfig=" + this.mUseSoftwareAutoBrightnessConfig);
        pw.println("  mSkipScreenOnBrightnessRamp=" + this.mSkipScreenOnBrightnessRamp);
        pw.println("  mColorFadeFadesConfig=" + this.mColorFadeFadesConfig);
        pw.println("  mColorFadeEnabled=" + this.mColorFadeEnabled);
        pw.println("  mIsDisplayInternal=" + this.mIsDisplayInternal);
        synchronized (this.mCachedBrightnessInfo) {
            pw.println("  mCachedBrightnessInfo.brightness=" + this.mCachedBrightnessInfo.brightness.value);
            pw.println("  mCachedBrightnessInfo.adjustedBrightness=" + this.mCachedBrightnessInfo.adjustedBrightness.value);
            pw.println("  mCachedBrightnessInfo.brightnessMin=" + this.mCachedBrightnessInfo.brightnessMin.value);
            pw.println("  mCachedBrightnessInfo.brightnessMax=" + this.mCachedBrightnessInfo.brightnessMax.value);
            pw.println("  mCachedBrightnessInfo.hbmMode=" + this.mCachedBrightnessInfo.hbmMode.value);
            pw.println("  mCachedBrightnessInfo.hbmTransitionPoint=" + this.mCachedBrightnessInfo.hbmTransitionPoint.value);
            pw.println("  mCachedBrightnessInfo.brightnessMaxReason =" + this.mCachedBrightnessInfo.brightnessMaxReason.value);
        }
        pw.println("  mDisplayBlanksAfterDozeConfig=" + this.mDisplayBlanksAfterDozeConfig);
        pw.println("  mBrightnessBucketsInDozeConfig=" + this.mBrightnessBucketsInDozeConfig);
        pw.println("  mDozeScaleFactor=" + this.mDozeScaleFactor);
        this.mHandler.runWithScissors(new Runnable() { // from class: com.android.server.display.DisplayPowerController$$ExternalSyntheticLambda5
            @Override // java.lang.Runnable
            public final void run() {
                DisplayPowerController.this.lambda$dump$11(pw);
            }
        }, 100L);
    }

    /* JADX INFO: Access modifiers changed from: private */
    /* renamed from: dumpLocal */
    public void lambda$dump$11(PrintWriter pw) {
        pw.println();
        pw.println("Display Power Controller Thread State:");
        pw.println("  mPowerRequest=" + this.mPowerRequest);
        pw.println("  mBrightnessReason=" + this.mBrightnessReason);
        pw.println("  mAppliedDimming=" + this.mAppliedDimming);
        pw.println("  mAppliedThrottling=" + this.mAppliedThrottling);
        pw.println("  mDozing=" + this.mDozing);
        pw.println("  mSkipRampState=" + skipRampStateToString(this.mSkipRampState));
        pw.println("  mScreenOnBlockStartRealTime=" + this.mScreenOnBlockStartRealTime);
        pw.println("  mScreenOffBlockStartRealTime=" + this.mScreenOffBlockStartRealTime);
        pw.println("  mPendingScreenOnUnblocker=" + this.mPendingScreenOnUnblocker);
        pw.println("  mPendingScreenOffUnblocker=" + this.mPendingScreenOffUnblocker);
        pw.println("  mPendingScreenOff=" + this.mPendingScreenOff);
        pw.println("  mReportedToPolicy=" + reportedToPolicyToString(this.mReportedScreenStateToPolicy));
        pw.println("  mIsRbcActive=" + this.mIsRbcActive);
        PrintWriter indentingPrintWriter = new IndentingPrintWriter(pw, "    ");
        this.mAutomaticBrightnessStrategy.dump(indentingPrintWriter);
        if (this.mScreenBrightnessRampAnimator != null) {
            pw.println("  mScreenBrightnessRampAnimator.isAnimating()=" + this.mScreenBrightnessRampAnimator.isAnimating());
        }
        if (this.mColorFadeOnAnimator != null) {
            pw.println("  mColorFadeOnAnimator.isStarted()=" + this.mColorFadeOnAnimator.isStarted());
        }
        if (this.mColorFadeOffAnimator != null) {
            pw.println("  mColorFadeOffAnimator.isStarted()=" + this.mColorFadeOffAnimator.isStarted());
        }
        if (this.mPowerState != null) {
            this.mPowerState.dump(pw);
        }
        if (this.mAutomaticBrightnessController != null) {
            this.mAutomaticBrightnessController.dump(pw);
            dumpBrightnessEvents(pw);
        }
        dumpRbcEvents(pw);
        if (this.mScreenOffBrightnessSensorController != null) {
            this.mScreenOffBrightnessSensorController.dump(pw);
        }
        if (this.mBrightnessRangeController != null) {
            this.mBrightnessRangeController.dump(pw);
        }
        if (this.mBrightnessThrottler != null) {
            this.mBrightnessThrottler.dump(pw);
        }
        pw.println();
        if (this.mDisplayWhiteBalanceController != null) {
            this.mDisplayWhiteBalanceController.dump(pw);
            this.mDisplayWhiteBalanceSettings.dump(pw);
        }
        this.mDpcExt.dump(pw);
        pw.println();
        if (this.mWakelockController != null) {
            this.mWakelockController.dumpLocal(pw);
        }
        pw.println();
        if (this.mDisplayBrightnessController != null) {
            this.mDisplayBrightnessController.dump(pw);
        }
        pw.println();
        if (this.mDisplayStateController != null) {
            this.mDisplayStateController.dumpsys(pw);
        }
        pw.println();
        if (this.mBrightnessClamperController != null) {
            this.mBrightnessClamperController.dump(indentingPrintWriter);
        }
    }

    private static String reportedToPolicyToString(int state) {
        switch (state) {
            case 0:
                return "REPORTED_TO_POLICY_SCREEN_OFF";
            case 1:
                return "REPORTED_TO_POLICY_SCREEN_TURNING_ON";
            case 2:
                return "REPORTED_TO_POLICY_SCREEN_ON";
            default:
                return Integer.toString(state);
        }
    }

    private static String skipRampStateToString(int state) {
        switch (state) {
            case 0:
                return "RAMP_STATE_SKIP_NONE";
            case 1:
                return "RAMP_STATE_SKIP_INITIAL";
            case 2:
                return "RAMP_STATE_SKIP_AUTOBRIGHT";
            default:
                return Integer.toString(state);
        }
    }

    private void dumpBrightnessEvents(PrintWriter pw) {
        if (this.mBrightnessEventRingBuffer == null) {
            return;
        }
        int size = this.mBrightnessEventRingBuffer.size();
        if (size < 1) {
            pw.println("No Automatic Brightness Adjustments");
            return;
        }
        pw.println("Automatic Brightness Adjustments Last " + size + " Events: ");
        BrightnessEvent[] eventArray = (BrightnessEvent[]) this.mBrightnessEventRingBuffer.toArray();
        for (int i = 0; i < this.mBrightnessEventRingBuffer.size(); i++) {
            pw.println("  " + eventArray[i].toString());
        }
    }

    private void dumpRbcEvents(PrintWriter pw) {
        int size = this.mRbcEventRingBuffer.size();
        if (size < 1) {
            pw.println("No Reduce Bright Colors Adjustments");
            return;
        }
        pw.println("Reduce Bright Colors Adjustments Last " + size + " Events: ");
        BrightnessEvent[] eventArray = (BrightnessEvent[]) this.mRbcEventRingBuffer.toArray();
        for (int i = 0; i < this.mRbcEventRingBuffer.size(); i++) {
            pw.println("  " + eventArray[i]);
        }
    }

    private void noteScreenState(int screenState, int reason) {
        FrameworkStatsLog.write((int) FrameworkStatsLog.SCREEN_STATE_CHANGED_V2, screenState, this.mDisplayStatsId, reason);
        if (this.mBatteryStats != null) {
            try {
                this.mBatteryStats.noteScreenState(screenState);
            } catch (RemoteException e) {
            }
        }
    }

    private void noteScreenBrightness(float brightness) {
        if (this.mBatteryStats != null) {
            try {
                this.mBatteryStats.noteScreenBrightness((int) brightness);
            } catch (RemoteException e) {
            }
        }
    }

    /* JADX INFO: Access modifiers changed from: private */
    public void reportStats(float brightness) {
        if (this.mLastStatsBrightness == brightness) {
            return;
        }
        synchronized (this.mCachedBrightnessInfo) {
            if (this.mCachedBrightnessInfo.hbmTransitionPoint == null) {
                return;
            }
            float hbmTransitionPoint = this.mCachedBrightnessInfo.hbmTransitionPoint.value;
            boolean aboveTransition = brightness > hbmTransitionPoint;
            boolean oldAboveTransition = this.mLastStatsBrightness > hbmTransitionPoint;
            if (aboveTransition || oldAboveTransition) {
                this.mLastStatsBrightness = brightness;
                this.mHandler.removeMessages(11);
                if (aboveTransition != oldAboveTransition) {
                    logHbmBrightnessStats(brightness, this.mDisplayStatsId);
                    return;
                }
                Message msg = this.mHandler.obtainMessage();
                msg.what = 11;
                msg.arg1 = Float.floatToIntBits(brightness);
                msg.arg2 = this.mDisplayStatsId;
                this.mHandler.sendMessageAtTime(msg, this.mClock.uptimeMillis() + 500);
            }
        }
    }

    /* JADX INFO: Access modifiers changed from: private */
    public void logHbmBrightnessStats(float brightness, int displayStatsId) {
        synchronized (this.mHandler) {
            FrameworkStatsLog.write((int) FrameworkStatsLog.DISPLAY_HBM_BRIGHTNESS_CHANGED, displayStatsId, brightness);
        }
    }

    private int nitsToRangeIndex(float nits) {
        for (int i = 0; i < BRIGHTNESS_RANGE_BOUNDARIES.length; i++) {
            if (nits < BRIGHTNESS_RANGE_BOUNDARIES[i]) {
                return BRIGHTNESS_RANGE_INDEX[i];
            }
        }
        return 38;
    }

    private int convertBrightnessReasonToStatsEnum(int brightnessReason) {
        switch (brightnessReason) {
            case 0:
                return 0;
            case 1:
                return 1;
            case 2:
                return 2;
            case 3:
                return 3;
            case 4:
                return 4;
            case 5:
                return 5;
            case 6:
                return 6;
            case 7:
                return 7;
            case 8:
                return 8;
            case 9:
                return 9;
            case 10:
                return 10;
            default:
                return 0;
        }
    }

    private void logBrightnessEvent(BrightnessEvent event, float unmodifiedBrightness) {
        float appliedHbmMaxNits;
        float appliedThermalCapNits;
        int modifier = event.getReason().getModifier();
        int flags = event.getFlags();
        boolean brightnessIsMax = unmodifiedBrightness == event.getHbmMax();
        float brightnessInNits = this.mDisplayBrightnessController.convertToAdjustedNits(event.getBrightness());
        float appliedLowPowerMode = event.isLowPowerModeSet() ? event.getPowerFactor() : -1.0f;
        int appliedRbcStrength = event.isRbcEnabled() ? event.getRbcStrength() : -1;
        if (event.getHbmMode() != 0) {
            appliedHbmMaxNits = this.mDisplayBrightnessController.convertToAdjustedNits(event.getHbmMax());
        } else {
            appliedHbmMaxNits = -1.0f;
        }
        if (event.getThermalMax() != 1.0f) {
            appliedThermalCapNits = this.mDisplayBrightnessController.convertToAdjustedNits(event.getThermalMax());
        } else {
            appliedThermalCapNits = -1.0f;
        }
        if (this.mIsDisplayInternal) {
            FrameworkStatsLog.write(FrameworkStatsLog.DISPLAY_BRIGHTNESS_CHANGED, this.mDisplayBrightnessController.convertToAdjustedNits(event.getInitialBrightness()), brightnessInNits, event.getLux(), event.getPhysicalDisplayId(), event.wasShortTermModelActive(), appliedLowPowerMode, appliedRbcStrength, appliedHbmMaxNits, appliedThermalCapNits, event.isAutomaticBrightnessEnabled(), 1, convertBrightnessReasonToStatsEnum(event.getReason().getReason()), nitsToRangeIndex(brightnessInNits), brightnessIsMax, event.getHbmMode() == 1, event.getHbmMode() == 2, (modifier & 2) > 0, this.mBrightnessClamperController.getBrightnessMaxReason(), (modifier & 1) > 0, event.isRbcEnabled(), (flags & 2) > 0, (flags & 4) > 0, (flags & 8) > 0, event.getAutoBrightnessMode() == 1, (flags & 32) > 0);
        }
    }

    private boolean readyToUpdateDisplayState() {
        return this.mDisplayId == 0 || this.mBootCompleted;
    }

    /* JADX INFO: Access modifiers changed from: private */
    /* loaded from: classes2.dex */
    public final class DisplayControllerHandler extends Handler {
        DisplayControllerHandler(Looper looper) {
            super(looper, null, true);
        }

        @Override // android.os.Handler
        public void handleMessage(Message msg) {
            DisplayPowerController.this.mDpcExt.onDisplayControllerHandler(msg, DisplayPowerController.this.mHandler);
            switch (msg.what) {
                case 1:
                    DisplayPowerController.this.lambda$new$0();
                    return;
                case 2:
                    if (DisplayPowerController.this.mPendingScreenOnUnblocker == msg.obj) {
                        if ((OplusPlatformLevelUtils.IS_LIGHT_OS || DisplayPowerController.IS_LIGHT_OS_BY_AMS) && DisplayPowerController.this.mPendingScreenOnUnblocker != null) {
                            DisplayPowerController.this.mDpcExt.setUxWhenWindowUnblock(true);
                        }
                        DisplayPowerController.this.unblockScreenOn();
                        DisplayPowerController.this.lambda$new$0();
                        return;
                    }
                    return;
                case 3:
                    if (DisplayPowerController.this.mPendingScreenOffUnblocker == msg.obj) {
                        DisplayPowerController.this.unblockScreenOff();
                        DisplayPowerController.this.lambda$new$0();
                        return;
                    }
                    return;
                case 4:
                    BrightnessConfiguration brightnessConfiguration = (BrightnessConfiguration) msg.obj;
                    DisplayPowerController.this.mAutomaticBrightnessStrategy.setBrightnessConfiguration(brightnessConfiguration, msg.arg1 == 1);
                    if (DisplayPowerController.this.mBrightnessTracker != null) {
                        BrightnessTracker brightnessTracker = DisplayPowerController.this.mBrightnessTracker;
                        if (brightnessConfiguration != null && brightnessConfiguration.shouldCollectColorSamples()) {
                            r1 = true;
                        }
                        brightnessTracker.setShouldCollectColorSample(r1);
                    }
                    DisplayPowerController.this.lambda$new$0();
                    return;
                case 5:
                    float temporaryBrightness = DisplayPowerController.this.mDpcExt.handleSetTemporaryBrightnessMessage(Float.intBitsToFloat(msg.arg1), "MSG_SET_TEMPORARY_BRIGHTNESS", DisplayPowerController.this.mDisplayId);
                    DisplayPowerController.this.mDisplayBrightnessController.setTemporaryBrightness(Float.valueOf(temporaryBrightness));
                    DisplayPowerController.this.lambda$new$0();
                    return;
                case 6:
                    DisplayPowerController.this.mAutomaticBrightnessStrategy.setTemporaryAutoBrightnessAdjustment(Float.intBitsToFloat(msg.arg1));
                    DisplayPowerController.this.lambda$new$0();
                    return;
                case 7:
                    DisplayPowerController.this.cleanupHandlerThreadAfterStop();
                    return;
                case 8:
                    if (DisplayPowerController.this.mStopped) {
                        return;
                    }
                    DisplayPowerController.this.handleSettingsChange();
                    return;
                case 9:
                    DisplayPowerController.this.handleRbcChanged();
                    return;
                case 10:
                    if (DisplayPowerController.this.mPowerState != null) {
                        float brightness = DisplayPowerController.this.mPowerState.getScreenBrightness();
                        DisplayPowerController.this.reportStats(brightness);
                        return;
                    }
                    return;
                case 11:
                    DisplayPowerController.this.logHbmBrightnessStats(Float.intBitsToFloat(msg.arg1), msg.arg2);
                    return;
                case 12:
                    float newBrightness = msg.obj instanceof Float ? ((Float) msg.obj).floatValue() : Float.NaN;
                    DisplayPowerController.this.handleOnSwitchUser(msg.arg1, msg.arg2, newBrightness);
                    return;
                case 13:
                    DisplayPowerController.this.mBootCompleted = true;
                    DisplayPowerController.this.lambda$new$0();
                    return;
                case 14:
                    r1 = msg.arg1 == 1;
                    DisplayPowerController.this.setDwbcStrongMode(msg.arg1);
                    return;
                case 15:
                    float cct = Float.intBitsToFloat(msg.arg1);
                    DisplayPowerController.this.setDwbcOverride(cct);
                    return;
                case 16:
                    DisplayPowerController.this.setDwbcLoggingEnabled(msg.arg1);
                    return;
                case 17:
                    if (DisplayPowerController.this.mDisplayBrightnessController.setBrightnessFromOffload(Float.intBitsToFloat(msg.arg1))) {
                        DisplayPowerController.this.lambda$new$0();
                        return;
                    }
                    return;
                case 18:
                    if (DisplayPowerController.this.mDisplayOffloadSession == msg.obj) {
                        DisplayPowerController.this.unblockScreenOnByDisplayOffload();
                        DisplayPowerController.this.lambda$new$0();
                        return;
                    }
                    return;
                case 19:
                case 20:
                default:
                    return;
                case 21:
                    if (DisplayPowerController.this.mResetFpsStatePending) {
                        DisplayPowerController.this.updateFpsWhenDcChange(false);
                        return;
                    }
                    return;
                case 22:
                    DisplayPowerController.this.mDpcExt.setRmMode();
                    return;
            }
        }
    }

    /* JADX INFO: Access modifiers changed from: private */
    /* loaded from: classes2.dex */
    public final class SettingsObserver extends ContentObserver {
        SettingsObserver(Handler handler) {
            super(handler);
        }

        @Override // android.database.ContentObserver
        public void onChange(boolean selfChange, Uri uri) {
            int state = DisplayPowerController.this.mPowerState != null ? DisplayPowerController.this.mPowerState.getScreenState() : 2;
            DisplayPowerController.this.mDpcExt.onChange(DisplayPowerController.this.mContext, DisplayPowerController.this.mDisplayId, selfChange, uri, state);
            if (uri.equals(Settings.System.getUriFor("screen_brightness_mode"))) {
                DisplayPowerController.this.mHandler.postAtTime(new Runnable() { // from class: com.android.server.display.DisplayPowerController$SettingsObserver$$ExternalSyntheticLambda0
                    @Override // java.lang.Runnable
                    public final void run() {
                        DisplayPowerController.SettingsObserver.this.lambda$onChange$0();
                    }
                }, DisplayPowerController.this.mClock.uptimeMillis());
            } else if (uri.equals(Settings.System.getUriFor("screen_brightness_for_als"))) {
                int preset = Settings.System.getIntForUser(DisplayPowerController.this.mContext.getContentResolver(), "screen_brightness_for_als", 2, -2);
                Slog.i(DisplayPowerController.this.mTag, "Setting up auto-brightness for preset " + DisplayBrightnessMappingConfig.autoBrightnessPresetToString(preset));
                DisplayPowerController.this.setUpAutoBrightness(DisplayPowerController.this.mContext, DisplayPowerController.this.mHandler);
                DisplayPowerController.this.sendUpdatePowerState();
            } else {
                DisplayPowerController.this.handleSettingsChange();
            }
        }

        /* JADX INFO: Access modifiers changed from: private */
        public /* synthetic */ void lambda$onChange$0() {
            DisplayPowerController.this.handleBrightnessModeChange();
            DisplayPowerController.this.lambda$new$0();
        }
    }

    /* JADX INFO: Access modifiers changed from: private */
    /* loaded from: classes2.dex */
    public final class ScreenOnUnblocker implements WindowManagerPolicy.ScreenOnListener {
        private ScreenOnUnblocker() {
        }

        @Override // com.android.server.policy.WindowManagerPolicy.ScreenOnListener
        public void onScreenOn() {
            Message msg = DisplayPowerController.this.mHandler.obtainMessage(2, this);
            if (DisplayPowerController.this.mDpcExt.sendMessageWhenScreenOnUnblocker(DisplayPowerController.this.mHandler, msg)) {
                return;
            }
            DisplayPowerController.this.mHandler.sendMessageAtTime(msg, DisplayPowerController.this.mClock.uptimeMillis());
        }
    }

    /* JADX INFO: Access modifiers changed from: private */
    /* loaded from: classes2.dex */
    public final class ScreenOffUnblocker implements WindowManagerPolicy.ScreenOffListener {
        private ScreenOffUnblocker() {
        }

        @Override // com.android.server.policy.WindowManagerPolicy.ScreenOffListener
        public void onScreenOff() {
            Message msg = DisplayPowerController.this.mHandler.obtainMessage(3, this);
            DisplayPowerController.this.mHandler.sendMessageAtTime(msg, DisplayPowerController.this.mClock.uptimeMillis());
        }
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void setAutoBrightnessLoggingEnabled(boolean enabled) {
        if (this.mAutomaticBrightnessController != null) {
            this.mAutomaticBrightnessController.setLoggingEnabled(enabled);
        }
        if (this.mDpcExt != null) {
            this.mDpcExt.setLoggingEnabled(enabled);
        }
        if (this.mScreenBrightnessRampAnimator != null) {
            this.mScreenBrightnessRampAnimator.setLoggingEnabled(enabled);
        }
        DEBUG_PANIC = enabled;
        Slog.d(this.mTag, "setLoggingEnabled loggingEnabled=" + enabled);
    }

    @Override // com.android.server.display.whitebalance.DisplayWhiteBalanceController.Callbacks
    public void updateWhiteBalance() {
        sendUpdatePowerState();
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void setDisplayWhiteBalanceLoggingEnabled(boolean enabled) {
        Message msg = this.mHandler.obtainMessage();
        msg.what = 16;
        msg.arg1 = enabled ? 1 : 0;
        msg.sendToTarget();
    }

    @Override // com.android.server.display.DisplayPowerControllerInterface
    public void setAmbientColorTemperatureOverride(float cct) {
        Message msg = this.mHandler.obtainMessage();
        msg.what = 15;
        msg.arg1 = Float.floatToIntBits(cct);
        msg.sendToTarget();
    }

    /* JADX INFO: Access modifiers changed from: package-private */
    /* loaded from: classes2.dex */
    public static class Injector {
        Injector() {
        }

        Clock getClock() {
            return new Clock() { // from class: com.android.server.display.DisplayPowerController$Injector$$ExternalSyntheticLambda0
                @Override // com.android.server.display.DisplayPowerController.Clock
                public final long uptimeMillis() {
                    return SystemClock.uptimeMillis();
                }
            };
        }

        DisplayPowerState getDisplayPowerState(DisplayBlanker blanker, ColorFade colorFade, int displayId, int displayState, IOplusDisplayPowerControllerExt dpcExt) {
            return new DisplayPowerState(blanker, colorFade, displayId, displayState, dpcExt);
        }

        RampAnimator.DualRampAnimator<DisplayPowerState> getDualRampAnimator(DisplayPowerState dps, FloatProperty<DisplayPowerState> firstProperty, FloatProperty<DisplayPowerState> secondProperty) {
            return new RampAnimator.DualRampAnimator<>(dps, firstProperty, secondProperty);
        }

        WakelockController getWakelockController(int displayId, DisplayManagerInternal.DisplayPowerCallbacks displayPowerCallbacks) {
            return new WakelockController(displayId, displayPowerCallbacks);
        }

        DisplayPowerProximityStateController getDisplayPowerProximityStateController(WakelockController wakelockController, DisplayDeviceConfig displayDeviceConfig, Looper looper, Runnable nudgeUpdatePowerState, int displayId, SensorManager sensorManager, IOplusDisplayPowerControllerExt dpcExt) {
            return new DisplayPowerProximityStateController(wakelockController, displayDeviceConfig, looper, nudgeUpdatePowerState, displayId, sensorManager, null, dpcExt);
        }

        AutomaticBrightnessController getAutomaticBrightnessController(AutomaticBrightnessController.Callbacks callbacks, Looper looper, SensorManager sensorManager, Sensor lightSensor, SparseArray<BrightnessMappingStrategy> brightnessMappingStrategyMap, int lightSensorWarmUpTime, float brightnessMin, float brightnessMax, float dozeScaleFactor, int lightSensorRate, int initialLightSensorRate, long brighteningLightDebounceConfig, long darkeningLightDebounceConfig, long brighteningLightDebounceConfigIdle, long darkeningLightDebounceConfigIdle, boolean resetAmbientLuxAfterWarmUpConfig, HysteresisLevels ambientBrightnessThresholds, HysteresisLevels screenBrightnessThresholds, HysteresisLevels ambientBrightnessThresholdsIdle, HysteresisLevels screenBrightnessThresholdsIdle, Context context, BrightnessRangeController brightnessModeController, BrightnessThrottler brightnessThrottler, int ambientLightHorizonShort, int ambientLightHorizonLong, float userLux, float userNits, BrightnessClamperController brightnessClamperController, DisplayManagerFlags displayManagerFlags) {
            return new AutomaticBrightnessController(callbacks, looper, sensorManager, lightSensor, brightnessMappingStrategyMap, lightSensorWarmUpTime, brightnessMin, brightnessMax, dozeScaleFactor, lightSensorRate, initialLightSensorRate, brighteningLightDebounceConfig, darkeningLightDebounceConfig, brighteningLightDebounceConfigIdle, darkeningLightDebounceConfigIdle, resetAmbientLuxAfterWarmUpConfig, ambientBrightnessThresholds, screenBrightnessThresholds, ambientBrightnessThresholdsIdle, screenBrightnessThresholdsIdle, context, brightnessModeController, brightnessThrottler, ambientLightHorizonShort, ambientLightHorizonLong, userLux, userNits, displayManagerFlags);
        }

        BrightnessMappingStrategy getDefaultModeBrightnessMapper(Context context, DisplayDeviceConfig displayDeviceConfig, DisplayWhiteBalanceController displayWhiteBalanceController) {
            return BrightnessMappingStrategy.create(context, displayDeviceConfig, 0, displayWhiteBalanceController);
        }

        ScreenOffBrightnessSensorController getScreenOffBrightnessSensorController(SensorManager sensorManager, Sensor lightSensor, Handler handler, ScreenOffBrightnessSensorController.Clock clock, int[] sensorValueToLux, BrightnessMappingStrategy brightnessMapper) {
            return new ScreenOffBrightnessSensorController(sensorManager, lightSensor, handler, clock, sensorValueToLux, brightnessMapper);
        }

        HighBrightnessModeController getHighBrightnessModeController(Handler handler, int width, int height, IBinder displayToken, String displayUniqueId, float brightnessMin, float brightnessMax, DisplayDeviceConfig.HighBrightnessModeData hbmData, HighBrightnessModeController.HdrBrightnessDeviceConfig hdrBrightnessCfg, Runnable hbmChangeCallback, HighBrightnessModeMetadata hbmMetadata, Context context) {
            return new HighBrightnessModeController(handler, width, height, displayToken, displayUniqueId, brightnessMin, brightnessMax, hbmData, hdrBrightnessCfg, hbmChangeCallback, hbmMetadata, context);
        }

        BrightnessRangeController getBrightnessRangeController(HighBrightnessModeController hbmController, Runnable modeChangeCallback, DisplayDeviceConfig displayDeviceConfig, Handler handler, DisplayManagerFlags flags, IBinder displayToken, DisplayDeviceInfo info) {
            return new BrightnessRangeController(hbmController, modeChangeCallback, displayDeviceConfig, handler, flags, displayToken, info);
        }

        BrightnessClamperController getBrightnessClamperController(Handler handler, BrightnessClamperController.ClamperChangeListener clamperChangeListener, BrightnessClamperController.DisplayDeviceData data, Context context, DisplayManagerFlags flags, SensorManager sensorManager, IOplusDisplayPowerControllerExt dpcExt) {
            return new BrightnessClamperController(handler, clamperChangeListener, data, context, flags, sensorManager, dpcExt);
        }

        DisplayWhiteBalanceController getDisplayWhiteBalanceController(Handler handler, SensorManager sensorManager, Resources resources) {
            return DisplayWhiteBalanceFactory.create(handler, sensorManager, resources);
        }

        boolean isColorFadeEnabled() {
            return !ActivityManager.isLowRamDeviceStatic();
        }
    }

    /* JADX INFO: Access modifiers changed from: package-private */
    /* loaded from: classes2.dex */
    public static class CachedBrightnessInfo {
        public MutableFloat brightness = new MutableFloat(Float.NaN);
        public MutableFloat adjustedBrightness = new MutableFloat(Float.NaN);
        public MutableFloat brightnessMin = new MutableFloat(Float.NaN);
        public MutableFloat brightnessMax = new MutableFloat(Float.NaN);
        public MutableInt hbmMode = new MutableInt(0);
        public MutableFloat hbmTransitionPoint = new MutableFloat(Float.POSITIVE_INFINITY);
        public MutableInt brightnessMaxReason = new MutableInt(0);

        CachedBrightnessInfo() {
        }

        public boolean checkAndSetFloat(MutableFloat mf, float f) {
            if (mf.value != f) {
                mf.value = f;
                return true;
            }
            return false;
        }

        public boolean checkAndSetInt(MutableInt mi, int i) {
            if (mi.value != i) {
                mi.value = i;
                return true;
            }
            return false;
        }
    }

    public IOplusDisplayPowerControllerWrapper getWrapper() {
        return this.mWrapper;
    }

    /* loaded from: classes2.dex */
    private class OplusDisplayPowerControllerWrapper implements IOplusDisplayPowerControllerWrapper {
        private LogicalDisplayMapper mLogicalDisplayMapper;

        private OplusDisplayPowerControllerWrapper() {
            this.mLogicalDisplayMapper = null;
        }

        @Override // com.android.server.display.IOplusDisplayPowerControllerWrapper
        public void setDebug(boolean val) {
            DisplayPowerController.DEBUG = val;
        }

        @Override // com.android.server.display.IOplusDisplayPowerControllerWrapper
        public void updatePowerState() {
            DisplayPowerController.this.lambda$new$0();
        }

        @Override // com.android.server.display.IOplusDisplayPowerControllerWrapper
        public void sendUpdatePowerState() {
            DisplayPowerController.this.sendUpdatePowerState();
        }

        @Override // com.android.server.display.IOplusDisplayPowerControllerWrapper
        public void animateScreenBrightness(float target, float sdrTarget, float rate) {
            DisplayPowerController.this.animateScreenBrightness(target, sdrTarget, rate);
        }

        @Override // com.android.server.display.IOplusDisplayPowerControllerWrapper
        public void setScreenBrightnessRangeMinimum(float val) {
            DisplayPowerController.this.mScreenBrightnessRangeMinimum = val;
        }

        @Override // com.android.server.display.IOplusDisplayPowerControllerWrapper
        public void setScreenBrightnessRangeMaximum(float val) {
            DisplayPowerController.this.mScreenBrightnessRangeMaximum = val;
        }

        @Override // com.android.server.display.IOplusDisplayPowerControllerWrapper
        public void setScreenBrightnessNormalMaximum(float val) {
            DisplayPowerController.this.mScreenBrightnessNormalMaximum = val;
        }

        @Override // com.android.server.display.IOplusDisplayPowerControllerWrapper
        public void setScreenBrightnessDefault(float val) {
            DisplayPowerController.this.mScreenBrightnessDefault = val;
        }

        @Override // com.android.server.display.IOplusDisplayPowerControllerWrapper
        public void setAutoBrightnessAdjustment(float val) {
            if (DisplayPowerController.this.mAutomaticBrightnessStrategy != null) {
                DisplayPowerController.this.mAutomaticBrightnessStrategy.setAutoBrightnessAdjustment(val);
            }
        }

        @Override // com.android.server.display.IOplusDisplayPowerControllerWrapper
        public void handleSettingsChange() {
            DisplayPowerController.this.handleSettingsChange();
        }

        @Override // com.android.server.display.IOplusDisplayPowerControllerWrapper
        public DisplayPowerProximityStateController getDisplayPowerProximityStateController() {
            return DisplayPowerController.this.mDisplayPowerProximityStateController;
        }

        @Override // com.android.server.display.IOplusDisplayPowerControllerWrapper
        public void setLogicalDisplayMapper(LogicalDisplayMapper mapper) {
            this.mLogicalDisplayMapper = mapper;
        }

        @Override // com.android.server.display.IOplusDisplayPowerControllerWrapper
        public LogicalDisplayMapper getLogicalDisplayMapper() {
            return this.mLogicalDisplayMapper;
        }

        @Override // com.android.server.display.IOplusDisplayPowerControllerWrapper
        public boolean isScreenOnUnblockerExist() {
            return DisplayPowerController.this.mPendingScreenOnUnblocker != null;
        }

        @Override // com.android.server.display.IOplusDisplayPowerControllerWrapper
        public void sendMsgUnblockScreenOn(boolean needBlockedScreenOn) {
            if (!needBlockedScreenOn && DisplayPowerController.this.mHandler.hasMessages(2)) {
                DisplayPowerController.this.mHandler.removeMessages(2);
                Message msg = DisplayPowerController.this.mHandler.obtainMessage(2, DisplayPowerController.this.mPendingScreenOnUnblocker);
                msg.setAsynchronous(true);
                DisplayPowerController.this.mHandler.sendMessage(msg);
                Slog.d(DisplayPowerController.this.mTag, "MSG_SCREEN_ON_UNBLOCKED sended");
            }
        }

        @Override // com.android.server.display.IOplusDisplayPowerControllerWrapper
        public int getDisplayId() {
            return DisplayPowerController.this.mDisplayId;
        }
    }
}
