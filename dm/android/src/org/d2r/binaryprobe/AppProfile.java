package org.d2r.binaryprobe;

import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Bundle;

/** APK-owned profile; ordinary phone builds keep their existing defaults. */
final class AppProfile {
    private static Bundle metadata(Context context) {
        try{return context.getPackageManager().getApplicationInfo(context.getPackageName(),PackageManager.GET_META_DATA).metaData;}
        catch(PackageManager.NameNotFoundException error){throw new IllegalStateException("Missing application profile",error);}
    }
    static boolean handheld(Context context){Bundle data=metadata(context);return data!=null&&data.getBoolean("org.d2r.handheld",false);}
    static int gameBuild(Context context){Bundle data=metadata(context);return data==null?91738:data.getInt("org.d2r.game_build",91738);}
}
