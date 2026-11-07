# 保留应用自身代码（无反射序列化，按需放开）
-keep class com.houyawei.nddrawing.** { *; }

# 注解与泛型签名保留，避免 Compose/协程运行期问题
-keepattributes Signature,InnerClasses,EnclosingMethod,*Annotation*,SourceFile,LineNumberTable

# Kotlin 协程/内联产生的类
-dontwarn org.jetbrains.kotlin.**
-keep class kotlin.coroutines.** { *; }
