package android.content;
public class Context {
 public Context getApplicationContext(){return this;}
 public java.io.File getFilesDir(){return new java.io.File(System.getProperty("java.io.tmpdir"));}
 public android.content.pm.ApplicationInfo getApplicationInfo(){return new android.content.pm.ApplicationInfo();}
 public String getPackageName(){return com.template.lsposed.TemplateConfig.TARGET_PACKAGES[0];}
}
