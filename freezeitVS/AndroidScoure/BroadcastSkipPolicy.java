package com.android.server.am;

import android.app.AppGlobals;
import android.app.AppOpsManager;
import android.content.AttributionSource;
import android.content.ComponentName;
import android.content.Intent;
import android.content.IntentSender;
import android.content.pm.PermissionInfo;
import android.content.pm.ResolveInfo;
import android.os.RemoteException;
import android.os.UserHandle;
import android.permission.IPermissionManager;
import android.permission.PermissionManager;
import android.util.Slog;
import com.android.internal.util.ArrayUtils;
import com.android.server.pm.PackageManagerService;
import java.util.Objects;
import system.ext.loader.core.ExtLoader;
/* loaded from: classes.dex */
public class BroadcastSkipPolicy {
    private IBroadcastSkipPolicyExt mBroadcastSkipPolicyExt = (IBroadcastSkipPolicyExt) ExtLoader.type(IBroadcastSkipPolicyExt.class).create();
    private PermissionManager mPermissionManager;
    private final ActivityManagerService mService;

    public BroadcastSkipPolicy(ActivityManagerService service) {
        this.mService = (ActivityManagerService) Objects.requireNonNull(service);
    }

    public String shouldSkipMessage(BroadcastRecord r, Object target) {
        if (target instanceof BroadcastFilter) {
            return shouldSkipMessage(r, (BroadcastFilter) target);
        }
        return shouldSkipMessage(r, (ResolveInfo) target);
    }

    /* JADX WARN: Removed duplicated region for block: B:110:0x04a7  */
    /* JADX WARN: Removed duplicated region for block: B:116:0x04e6  */
    /* JADX WARN: Removed duplicated region for block: B:178:0x0660  */
    /* JADX WARN: Removed duplicated region for block: B:226:0x061c A[SYNTHETIC] */
    /*
        Code decompiled incorrectly, please refer to instructions dump.
        To view partially-correct add '--show-bad-code' argument
    */
    private java.lang.String shouldSkipMessage(com.android.server.am.BroadcastRecord r28, android.content.pm.ResolveInfo r29) {
        /*
            Method dump skipped, instructions count: 1786
            To view this dump add '--comments-level debug' option
        */
        throw new UnsupportedOperationException("Method not decompiled: com.android.server.am.BroadcastSkipPolicy.shouldSkipMessage(com.android.server.am.BroadcastRecord, android.content.pm.ResolveInfo):java.lang.String");
    }

    public boolean disallowBackgroundStart(BroadcastRecord r) {
        return (r.intent.getFlags() & 8388608) != 0 || (r.intent.getComponent() == null && r.intent.getPackage() == null && (r.intent.getFlags() & 16777216) == 0 && !isSignaturePerm(r.requiredPermissions));
    }

    private String shouldSkipMessage(BroadcastRecord r, BroadcastFilter filter) {
        String str;
        String str2;
        String str3;
        String str4;
        String str5;
        String str6;
        BroadcastSkipPolicy broadcastSkipPolicy;
        AttributionSource attributionSource;
        String str7;
        int i;
        String str8;
        AttributionSource attributionSource2;
        int perm;
        String str9;
        String str10;
        BroadcastSkipPolicy broadcastSkipPolicy2 = this;
        if (r.options != null && !r.options.testRequireCompatChange(filter.owningUid)) {
            return "Compat change filtered: broadcasting " + r.intent.toString() + " to uid " + filter.owningUid + " due to compat change " + r.options.getRequireCompatChangeId();
        }
        if (!broadcastSkipPolicy2.mService.validateAssociationAllowedLocked(r.callerPackage, r.callingUid, filter.packageName, filter.owningUid)) {
            return "Association not allowed: broadcasting " + r.intent.toString() + " from " + r.callerPackage + " (pid=" + r.callingPid + ", uid=" + r.callingUid + ") to " + filter.packageName + " through " + filter;
        }
        if (!broadcastSkipPolicy2.mService.mIntentFirewall.checkBroadcast(r.intent, r.callingUid, r.callingPid, r.resolvedType, filter.receiverList.uid)) {
            return "Firewall blocked: broadcasting " + r.intent.toString() + " from " + r.callerPackage + " (pid=" + r.callingPid + ", uid=" + r.callingUid + ") to " + filter.packageName + " through " + filter;
        }
        String str11 = ") requires ";
        String str12 = ") requires appop ";
        if (filter.requiredPermission != null) {
            if (ActivityManagerService.checkComponentPermission(filter.requiredPermission, r.callingPid, r.callingUid, -1, true) != 0) {
                return "Permission Denial: broadcasting " + r.intent.toString() + " from " + r.callerPackage + " (pid=" + r.callingPid + ", uid=" + r.callingUid + ") requires " + filter.requiredPermission + " due to registered receiver " + filter;
            }
            int opCode = AppOpsManager.permissionToOpCode(filter.requiredPermission);
            if (opCode != -1 && broadcastSkipPolicy2.mService.getAppOpsManager().noteOpNoThrow(opCode, r.callingUid, r.callerPackage, r.callerFeatureId, "Broadcast sent to protected receiver") != 0) {
                return "Appop Denial: broadcasting " + r.intent.toString() + " from " + r.callerPackage + " (pid=" + r.callingPid + ", uid=" + r.callingUid + ") requires appop " + AppOpsManager.permissionToOp(filter.requiredPermission) + " due to registered receiver " + filter;
            }
        }
        if (filter.receiverList.app == null || filter.receiverList.app.isKilled() || filter.receiverList.app.mErrorState.isCrashing()) {
            if (filter.receiverList.app != null) {
                Slog.w(BroadcastQueue.TAG, "Process state: app.isKilled = " + filter.receiverList.app.isKilled() + " app.mErrorState.isCrashing = " + filter.receiverList.app.mErrorState.isCrashing());
            }
            return "Skipping deliver [" + r.queue.toString() + "] " + r + " to " + filter.receiverList + ": process gone or crashing";
        }
        boolean visibleToInstantApps = (r.intent.getFlags() & 2097152) != 0;
        if (!visibleToInstantApps && filter.instantApp && filter.receiverList.uid != r.callingUid) {
            return "Instant App Denial: receiving " + r.intent.toString() + " to " + filter.receiverList.app + " (pid=" + filter.receiverList.pid + ", uid=" + filter.receiverList.uid + ") due to sender " + r.callerPackage + " (uid " + r.callingUid + ") not specifying FLAG_RECEIVER_VISIBLE_TO_INSTANT_APPS";
        }
        if (!filter.visibleToInstantApp && r.callerInstantApp && filter.receiverList.uid != r.callingUid) {
            return "Instant App Denial: receiving " + r.intent.toString() + " to " + filter.receiverList.app + " (pid=" + filter.receiverList.pid + ", uid=" + filter.receiverList.uid + ") requires receiver be visible to instant apps due to sender " + r.callerPackage + " (uid " + r.callingUid + ")";
        }
        String str13 = "Permission Denial: receiving ";
        String str14 = "Appop Denial: receiving ";
        String str15 = "Broadcast delivered to registered receiver ";
        if (r.requiredPermissions == null || r.requiredPermissions.length <= 0) {
            str = "Broadcast delivered to registered receiver ";
            str2 = "Permission Denial: receiving ";
            str3 = ") due to sender ";
            str4 = "Appop Denial: receiving ";
            str5 = ") requires appop ";
        } else {
            if (Flags.usePermissionManagerForBroadcastDeliveryCheck()) {
                str3 = ") due to sender ";
                attributionSource = new AttributionSource.Builder(filter.receiverList.uid).setPid(filter.receiverList.pid).setPackageName(filter.packageName).setAttributionTag(filter.featureId).build();
            } else {
                str3 = ") due to sender ";
                attributionSource = null;
            }
            int i2 = 0;
            while (true) {
                String str16 = str12;
                if (i2 >= r.requiredPermissions.length) {
                    str = str15;
                    str2 = str13;
                    str4 = str14;
                    str5 = str16;
                    break;
                }
                String requiredPermission = r.requiredPermissions[i2];
                if (Flags.usePermissionManagerForBroadcastDeliveryCheck()) {
                    i = i2;
                    str8 = str14;
                    str7 = str15;
                    if (broadcastSkipPolicy2.hasPermissionForDataDelivery(requiredPermission, str15 + filter.receiverId, attributionSource)) {
                        perm = 0;
                    } else {
                        perm = -1;
                    }
                    attributionSource2 = attributionSource;
                } else {
                    str7 = str15;
                    i = i2;
                    str8 = str14;
                    attributionSource2 = attributionSource;
                    perm = ActivityManagerService.checkComponentPermission(requiredPermission, filter.receiverList.pid, filter.receiverList.uid, -1, true);
                }
                if (perm != 0) {
                    return str13 + r.intent.toString() + " to " + filter.receiverList.app + " (pid=" + filter.receiverList.pid + ", uid=" + filter.receiverList.uid + str11 + requiredPermission + " due to sender " + r.callerPackage + " (uid " + r.callingUid + ")";
                }
                if (Flags.usePermissionManagerForBroadcastDeliveryCheck()) {
                    str9 = str11;
                    str10 = str13;
                } else {
                    int appOp = AppOpsManager.permissionToOpCode(requiredPermission);
                    if (appOp == -1 || appOp == r.appOp) {
                        str9 = str11;
                        str10 = str13;
                    } else {
                        str9 = str11;
                        str10 = str13;
                        if (broadcastSkipPolicy2.mService.getAppOpsManager().noteOpNoThrow(appOp, filter.receiverList.uid, filter.packageName, filter.featureId, str7 + filter.receiverId) != 0) {
                            return str8 + r.intent.toString() + " to " + filter.receiverList.app + " (pid=" + filter.receiverList.pid + ", uid=" + filter.receiverList.uid + str16 + AppOpsManager.permissionToOp(requiredPermission) + " due to sender " + r.callerPackage + " (uid " + r.callingUid + ")";
                        }
                    }
                }
                i2 = i + 1;
                str14 = str8;
                str12 = str16;
                str15 = str7;
                attributionSource = attributionSource2;
                str11 = str9;
                str13 = str10;
            }
        }
        if ((r.requiredPermissions == null || r.requiredPermissions.length == 0) && ActivityManagerService.checkComponentPermission(null, filter.receiverList.pid, filter.receiverList.uid, -1, true) != 0) {
            return "Permission Denial: security check failed when receiving " + r.intent.toString() + " to " + filter.receiverList.app + " (pid=" + filter.receiverList.pid + ", uid=" + filter.receiverList.uid + str3 + r.callerPackage + " (uid " + r.callingUid + ")";
        }
        if (r.excludedPermissions == null || r.excludedPermissions.length <= 0) {
            str6 = str5;
        } else {
            int i3 = 0;
            while (i3 < r.excludedPermissions.length) {
                String excludedPermission = r.excludedPermissions[i3];
                String str17 = str5;
                int perm2 = ActivityManagerService.checkComponentPermission(excludedPermission, filter.receiverList.pid, filter.receiverList.uid, -1, true);
                int appOp2 = AppOpsManager.permissionToOpCode(excludedPermission);
                if (appOp2 != -1) {
                    if (perm2 == 0 && broadcastSkipPolicy2.mService.getAppOpsManager().checkOpNoThrow(appOp2, filter.receiverList.uid, filter.packageName) == 0) {
                        return str4 + r.intent.toString() + " to " + filter.receiverList.app + " (pid=" + filter.receiverList.pid + ", uid=" + filter.receiverList.uid + ") excludes appop " + AppOpsManager.permissionToOp(excludedPermission) + " due to sender " + r.callerPackage + " (uid " + r.callingUid + ")";
                    }
                } else if (perm2 == 0) {
                    return str2 + r.intent.toString() + " to " + filter.receiverList.app + " (pid=" + filter.receiverList.pid + ", uid=" + filter.receiverList.uid + ") excludes " + excludedPermission + " due to sender " + r.callerPackage + " (uid " + r.callingUid + ")";
                }
                i3++;
                broadcastSkipPolicy2 = this;
                str2 = str2;
                str5 = str17;
            }
            str6 = str5;
        }
        if (r.excludedPackages != null && r.excludedPackages.length > 0 && ArrayUtils.contains(r.excludedPackages, filter.packageName)) {
            return "Skipping delivery of excluded package " + r.intent.toString() + " to " + filter.receiverList.app + " (pid=" + filter.receiverList.pid + ", uid=" + filter.receiverList.uid + ") excludes package " + filter.packageName + " due to sender " + r.callerPackage + " (uid " + r.callingUid + ")";
        }
        if (r.appOp == -1) {
            broadcastSkipPolicy = this;
        } else {
            broadcastSkipPolicy = this;
            if (broadcastSkipPolicy.mService.getAppOpsManager().noteOpNoThrow(r.appOp, filter.receiverList.uid, filter.packageName, filter.featureId, str + filter.receiverId) != 0) {
                return str4 + r.intent.toString() + " to " + filter.receiverList.app + " (pid=" + filter.receiverList.pid + ", uid=" + filter.receiverList.uid + str6 + AppOpsManager.opToName(r.appOp) + " due to sender " + r.callerPackage + " (uid " + r.callingUid + ")";
            }
        }
        int originalCallingUid = r.sticky ? r.originalStickyCallingUid : r.callingUid;
        if (!filter.exported && ActivityManagerService.checkComponentPermission(null, r.callingPid, originalCallingUid, filter.receiverList.uid, filter.exported) != 0) {
            return "Exported Denial: sending " + r.intent.toString() + ", action: " + r.intent.getAction() + " from " + r.callerPackage + " (uid=" + originalCallingUid + ") due to receiver " + filter.receiverList.app + " (uid " + filter.receiverList.uid + ") not specifying RECEIVER_EXPORTED";
        }
        if (!broadcastSkipPolicy.requestStartTargetPermissionsReviewIfNeededLocked(r, filter.packageName, filter.owningUserId)) {
            return "Skipping delivery to " + filter.packageName + " due to permissions review";
        }
        return broadcastSkipPolicy.mBroadcastSkipPolicyExt.shouldSkipMessage(r, filter);
    }

    private static String broadcastDescription(BroadcastRecord r, ComponentName component) {
        return r.intent.toString() + " from " + r.callerPackage + " (pid=" + r.callingPid + ", uid=" + r.callingUid + ") to " + component.flattenToShortString();
    }

    private boolean noteOpForManifestReceiver(int appOp, BroadcastRecord r, ResolveInfo info, ComponentName component) {
        String[] strArr;
        if (ArrayUtils.isEmpty(info.activityInfo.attributionTags)) {
            return noteOpForManifestReceiverInner(appOp, r, info, component, null);
        }
        for (String tag : info.activityInfo.attributionTags) {
            if (!noteOpForManifestReceiverInner(appOp, r, info, component, tag)) {
                return false;
            }
        }
        return true;
    }

    private boolean noteOpForManifestReceiverInner(int appOp, BroadcastRecord r, ResolveInfo info, ComponentName component, String tag) {
        if (this.mService.getAppOpsManager().noteOpNoThrow(appOp, info.activityInfo.applicationInfo.uid, info.activityInfo.packageName, tag, "Broadcast delivered to " + info.activityInfo.name) != 0) {
            Slog.w(BroadcastQueue.TAG, "Appop Denial: receiving " + r.intent + " to " + component.flattenToShortString() + " requires appop " + AppOpsManager.opToName(appOp) + " due to sender " + r.callerPackage + " (uid " + r.callingUid + ")");
            return false;
        }
        return true;
    }

    private static boolean isSignaturePerm(String[] perms) {
        if (perms == null) {
            return false;
        }
        IPermissionManager pm = AppGlobals.getPermissionManager();
        for (int i = perms.length - 1; i >= 0; i--) {
            try {
                PermissionInfo pi = pm.getPermissionInfo(perms[i], PackageManagerService.PLATFORM_PACKAGE_NAME, 0);
                if (pi == null || (pi.protectionLevel & 31) != 2) {
                    return false;
                }
            } catch (RemoteException e) {
                return false;
            }
        }
        return true;
    }

    private boolean requestStartTargetPermissionsReviewIfNeededLocked(BroadcastRecord receiverRecord, String receivingPackageName, final int receivingUserId) {
        boolean callerForeground;
        if (this.mService.getPackageManagerInternal().isPermissionsReviewRequired(receivingPackageName, receivingUserId)) {
            if (receiverRecord.callerApp != null) {
                callerForeground = receiverRecord.callerApp.mState.getSetSchedGroup() != 0;
            } else {
                callerForeground = true;
            }
            if (!callerForeground || receiverRecord.intent.getComponent() == null) {
                Slog.w(BroadcastQueue.TAG, "u" + receivingUserId + " Receiving a broadcast in package" + receivingPackageName + " requires a permissions review");
            } else {
                PendingIntentRecord intentSender = this.mService.mPendingIntentController.getIntentSender(1, receiverRecord.callerPackage, receiverRecord.callerFeatureId, receiverRecord.callingUid, receiverRecord.userId, null, null, 0, new Intent[]{receiverRecord.intent}, new String[]{receiverRecord.intent.resolveType(this.mService.mContext.getContentResolver())}, 1409286144, null);
                final Intent intent = new Intent("android.intent.action.REVIEW_PERMISSIONS");
                intent.addFlags(411041792);
                intent.putExtra("android.intent.extra.PACKAGE_NAME", receivingPackageName);
                intent.putExtra("android.intent.extra.INTENT", new IntentSender(intentSender));
                if (ActivityManagerDebugConfig.DEBUG_PERMISSIONS_REVIEW) {
                    Slog.i(BroadcastQueue.TAG, "u" + receivingUserId + " Launching permission review for package " + receivingPackageName);
                }
                this.mService.mHandler.post(new Runnable() { // from class: com.android.server.am.BroadcastSkipPolicy.1
                    @Override // java.lang.Runnable
                    public void run() {
                        BroadcastSkipPolicy.this.mService.mContext.startActivityAsUser(intent, new UserHandle(receivingUserId));
                    }
                });
            }
            return false;
        }
        return true;
    }

    private PermissionManager getPermissionManager() {
        if (this.mPermissionManager == null) {
            this.mPermissionManager = (PermissionManager) this.mService.mContext.getSystemService(PermissionManager.class);
        }
        return this.mPermissionManager;
    }

    private boolean hasPermissionForDataDelivery(String permission, String message, AttributionSource... attributionSources) {
        PermissionManager permissionManager = getPermissionManager();
        if (permissionManager == null) {
            return false;
        }
        for (AttributionSource attributionSource : attributionSources) {
            int permissionCheckResult = permissionManager.checkPermissionForDataDelivery(permission, attributionSource, message);
            if (permissionCheckResult != 0) {
                return false;
            }
        }
        return true;
    }

    private AttributionSource[] createAttributionSourcesForResolveInfo(ResolveInfo info) {
        String[] attributionTags = info.activityInfo.attributionTags;
        if (ArrayUtils.isEmpty(attributionTags)) {
            return new AttributionSource[]{new AttributionSource.Builder(info.activityInfo.applicationInfo.uid).setPackageName(info.activityInfo.packageName).build()};
        }
        AttributionSource[] attributionSources = new AttributionSource[attributionTags.length];
        for (int i = 0; i < attributionTags.length; i++) {
            attributionSources[i] = new AttributionSource.Builder(info.activityInfo.applicationInfo.uid).setPackageName(info.activityInfo.packageName).setAttributionTag(attributionTags[i]).build();
        }
        return attributionSources;
    }
}
