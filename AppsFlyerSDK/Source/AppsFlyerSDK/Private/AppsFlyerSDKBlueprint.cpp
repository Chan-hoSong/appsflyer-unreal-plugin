#include "AppsFlyerSDKBlueprint.h"
#include "Runtime/Engine/Classes/Kismet/KismetSystemLibrary.h"
#include "Engine/GameEngine.h"
#include "AppsFlyerSDKSettings.h"
// #include "EngineMinimal.h"
#include "Logging/LogMacros.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "AppsFlyerConversionData.h"
#include "AppsFlyerSDKCallbacks.h"
#if PLATFORM_ANDROID
#include "Android/AndroidJNI.h"
#include "Android/AndroidApplication.h"
#include "Android/AndroidJava.h"
#elif PLATFORM_IOS
#import <AppsFlyerLib/AppsFlyerLib.h>
#import "UE4AFSDKDelegate.h"
#include "IOSAppDelegate.h"
#import <objc/message.h>
// SKAdNewtork request configuration workaround
typedef void (*bypassDidFinishLaunchingWithOption)(id, SEL, NSInteger);

static inline BOOL AppsFlyerIsEmptyValue(id obj) {
    return obj == nil
    || (NSNull *)obj == [NSNull null]
    || ([obj respondsToSelector:@selector(length)] && [obj length] == 0)
    || ([obj respondsToSelector:@selector(count)] && [obj count] == 0);
}

#endif
DEFINE_LOG_CATEGORY(LogAppsFlyerSDKBlueprint);

#if PLATFORM_ANDROID
extern "C" {
    JNIEXPORT void JNICALL Java_com_appsflyer_AppsFlyer2dXConversionCallback_onInstallConversionDataLoadedNative
    (JNIEnv *env, jobject obj, jobject attributionObject) {
        TMap<FString, FString> map;
        FAppsFlyerConversionData conversionData;
        // Java map to UE4 map
        jclass clsHashMap = env->GetObjectClass(attributionObject);
        jmethodID midKeySet = env->GetMethodID(clsHashMap, "keySet", "()Ljava/util/Set;");
        auto objKeySet = FScopedJavaObject<jobject>(env, env->CallObjectMethod(attributionObject, midKeySet));

        jclass clsSet = env->GetObjectClass(*objKeySet);
        jmethodID midToArray = env->GetMethodID(clsSet, "toArray", "()[Ljava/lang/Object;");
        auto arrayOfKeys = FScopedJavaObject<jobjectArray>(env, (jobjectArray)env->CallObjectMethod(*objKeySet, midToArray));
        int32 arraySize = env->GetArrayLength(*arrayOfKeys);
        int32 i = 0;
        while (i < arraySize)
        {
      	    auto key = FJavaHelper::FStringFromLocalRef(env, (jstring)(env->GetObjectArrayElement(*arrayOfKeys, i++)));
      	    auto value = FJavaHelper::FStringFromLocalRef(env, (jstring)(env->GetObjectArrayElement(*arrayOfKeys, i++)));

      	    map.Add(*key, *value);
        }

        // Java map to UE4 map
        conversionData.InstallData = map;
        for (TObjectIterator<UAppsFlyerSDKCallbacks> Itr; Itr; ++Itr) {
            Itr->OnConversionDataReceived.Broadcast(conversionData);
        }
    }
    JNIEXPORT void JNICALL Java_com_appsflyer_AppsFlyer2dXConversionCallback_onInstallConversionFailureNative
    (JNIEnv *env, jobject obj, jstring stringError) {
        const char *convertedValue = (env)->GetStringUTFChars(stringError, 0);
        for (TObjectIterator<UAppsFlyerSDKCallbacks> Itr; Itr; ++Itr) {
            Itr->OnConversionDataRequestFailure.Broadcast(convertedValue);
        }
		env->ReleaseStringUTFChars(stringError, convertedValue);
    }
    JNIEXPORT void JNICALL Java_com_appsflyer_AppsFlyer2dXConversionCallback_onAppOpenAttributionNative
    (JNIEnv *env, jobject obj, jobject attributionObject) {}
    JNIEXPORT void JNICALL Java_com_appsflyer_AppsFlyer2dXConversionCallback_onAttributionFailureNative
    (JNIEnv *env, jobject obj, jobject stringError) {}
}
#elif PLATFORM_IOS
@protocol AppsFlyerLibDelegate;

static void OnOpenURL(UIApplication *application, NSURL *url, NSString *sourceApplication, id annotation)
{
    dispatch_async(dispatch_get_main_queue(), ^ {
        [[AppsFlyerLib shared] handleOpenURL:url sourceApplication:sourceApplication withAnnotation:annotation];
    });
}

static void onConversionDataSuccess(NSDictionary *installData) {
    TMap<FString, FString> map;
    FAppsFlyerConversionData conversionData;
    for (NSString * key in [installData allKeys]) {
        NSString *objcValue = [NSString stringWithFormat:@"%@", [installData objectForKey:key]];
        FString ueKey(key);
        FString ueValue(objcValue);
        map.Add(*ueKey, *ueValue);
    }
    conversionData.InstallData = map;
    for (TObjectIterator<UAppsFlyerSDKCallbacks> Itr; Itr; ++Itr) {
        Itr->OnConversionDataReceived.Broadcast(conversionData);
    }
}
static void onConversionDataFail(NSString *error) {
    NSLog(@"%@", error);
}
static void onAppOpenAttribution(NSDictionary *attributionData) {
    TMap<FString, FString> map;
    FAppsFlyerConversionData conversionData;
    for (NSString * key in [attributionData allKeys]) {
        NSString *objcValue = [NSString stringWithFormat:@"%@", [attributionData objectForKey:key]];
        FString ueKey(key);
        FString ueValue(objcValue);
        map.Add(*ueKey, *ueValue);
    }
    conversionData.InstallData = map;
    for (TObjectIterator<UAppsFlyerSDKCallbacks> Itr; Itr; ++Itr) {
        Itr->OnConversionDataReceived.Broadcast(conversionData);
    }
}
static void onAppOpenAttributionFailure(NSString *error) {
    NSLog(@"%@", error);
}
#endif
UAppsFlyerSDKBlueprint::UAppsFlyerSDKBlueprint(const FObjectInitializer &ObjectInitializer) : Super(ObjectInitializer) {}

void UAppsFlyerSDKBlueprint::configure()
{
    const UAppsFlyerSDKSettings *defaultSettings = GetDefault<UAppsFlyerSDKSettings>();
    const bool isDebug = defaultSettings->bIsDebug;

#if PLATFORM_ANDROID
    JNIEnv* env = FAndroidApplication::GetJavaEnv();
    jmethodID appsflyer =
        FJavaWrapper::FindMethod(env, FJavaWrapper::GameActivityClassID, "afStart", "(Ljava/lang/String;Z)V", false);
    auto key = FJavaClassObject::GetJString(defaultSettings->appsFlyerDevKey);

    FJavaWrapper::CallVoidMethod(env, FJavaWrapper::GameActivityThis, appsflyer, *key, isDebug);


#elif PLATFORM_IOS
    dispatch_async(dispatch_get_main_queue(), ^ {
        if (!defaultSettings->appsFlyerDevKey.IsEmpty() && !defaultSettings->appleAppID.IsEmpty()) {
            [AppsFlyerLib shared].disableSKAdNetwork = defaultSettings->bDisableSKAdNetwork;
            [AppsFlyerLib shared].appsFlyerDevKey = defaultSettings->appsFlyerDevKey.GetNSString();
            [AppsFlyerLib shared].appleAppID = defaultSettings->appleAppID.GetNSString();
            [AppsFlyerLib shared].isDebug = isDebug;
            // Set currency code if value not `empty`
            NSString *currencyCode = defaultSettings->currencyCode.GetNSString();
            if (!AppsFlyerIsEmptyValue(currencyCode)) {
                [AppsFlyerLib shared].currencyCode = currencyCode;
            }

            FIOSCoreDelegates::OnOpenURL.AddStatic(&OnOpenURL);
            UE4AFSDKDelegate *delegate = [[UE4AFSDKDelegate alloc] init];
            delegate.onConversionDataSuccess = onConversionDataSuccess;
            delegate.onConversionDataFail = onConversionDataFail;
            delegate.onAppOpenAttribution = onAppOpenAttribution;
            delegate.onAppOpenAttributionFailure = onAppOpenAttributionFailure;
            [AppsFlyerLib shared].delegate = (id<AppsFlyerLibDelegate>)delegate;

            // SKAdNewtork request configuration workaround
            SEL SKSel = NSSelectorFromString(@"__willResolveSKRules:");
            id AppsFlyer = [AppsFlyerLib shared];
            if ([AppsFlyer respondsToSelector:SKSel]) {
                bypassDidFinishLaunchingWithOption msgSend = (bypassDidFinishLaunchingWithOption)objc_msgSend;
                msgSend(AppsFlyer, SKSel, 2);
            }

            UE_LOG(LogAppsFlyerSDKBlueprint, Display, TEXT("AppsFlyer: UE4 ready"));

            [[AppsFlyerLib shared] start];
            [[NSNotificationCenter defaultCenter] addObserverForName: UIApplicationWillEnterForegroundNotification
            object: nil
            queue: nil
            usingBlock: ^ (NSNotification * note) {
                UE_LOG(LogAppsFlyerSDKBlueprint, Display, TEXT("UIApplicationWillEnterForegroundNotification"));
                [[AppsFlyerLib shared] start];
            }];
        }
    });
#endif
}

void UAppsFlyerSDKBlueprint::start() {
#if PLATFORM_ANDROID
    JNIEnv* env = FAndroidApplication::GetJavaEnv();
    jmethodID start = FJavaWrapper::FindMethod(env,
                               FJavaWrapper::GameActivityClassID,
                               "afStartLaunch",
                               "()V", false);
    FJavaWrapper::CallVoidMethod(env, FJavaWrapper::GameActivityThis, start);
#elif PLATFORM_IOS
    dispatch_async(dispatch_get_main_queue(), ^ {
        [[AppsFlyerLib shared] start];
    });
#endif
}
void UAppsFlyerSDKBlueprint::setCustomerUserId(FString customerUserId) {
#if PLATFORM_ANDROID
    JNIEnv* env = FAndroidApplication::GetJavaEnv();
    jmethodID setCustomerUserId = FJavaWrapper::FindMethod(env,
                                  FJavaWrapper::GameActivityClassID,
                                  "afSetCustomerUserId",
                                  "(Ljava/lang/String;)V", false);
	auto jCustomerUserId = FJavaClassObject::GetJString(customerUserId);
    FJavaWrapper::CallVoidMethod(env, FJavaWrapper::GameActivityThis, setCustomerUserId, *jCustomerUserId);
#elif PLATFORM_IOS
    dispatch_async(dispatch_get_main_queue(), ^ {
        [[AppsFlyerLib shared] setCustomerUserID:customerUserId.GetNSString()];
    });
#endif
}
void UAppsFlyerSDKBlueprint::logEvent(FString eventName, TMap <FString, FString> values) {
#if PLATFORM_ANDROID
    JNIEnv* env = FAndroidApplication::GetJavaEnv();
    jmethodID logEvent = FJavaWrapper::FindMethod(env,
                           FJavaWrapper::GameActivityClassID,
                           "afLogEvent",
                           "(Ljava/lang/String;Ljava/util/Map;)V", false);
    auto jEventName = FJavaClassObject::GetJString(eventName);
    jclass mapClass = env->FindClass("java/util/HashMap");
    jmethodID mapConstructor = env->GetMethodID(mapClass, "<init>", "()V");
    auto map = FScopedJavaObject<jobject>(env, env->NewObject(mapClass, mapConstructor));
    jmethodID putMethod = env->GetMethodID(mapClass, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    for (const TPair<FString, FString>& pair : values) {
        auto Key = FJavaClassObject::GetJString(pair.Key);
        auto Value = FJavaClassObject::GetJString(pair.Value);
        env->CallObjectMethod(*map, putMethod, *Key, *Value);
    }
    FJavaWrapper::CallVoidMethod(env, FJavaWrapper::GameActivityThis, logEvent, *jEventName, *map);
#elif PLATFORM_IOS
    dispatch_async(dispatch_get_main_queue(), ^ {
        NSMutableDictionary *dictionary = [NSMutableDictionary dictionary];
        for (const TPair<FString, FString>& pair : values) {
            [dictionary setValue:pair.Value.GetNSString() forKey:pair.Key.GetNSString()];
        }

        // Transform `af_revenue` value to NSNumber in case if value of NSString type
        id revenueString = dictionary[@"af_revenue"];
        if (revenueString && [revenueString isKindOfClass:[NSString class]]) {
            NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
            formatter.numberStyle = NSNumberFormatterDecimalStyle;
            NSNumber *revenueNumber = [formatter numberFromString:revenueString];
            dictionary[@"af_revenue"] = revenueNumber;
        }

        [[AppsFlyerLib shared] logEvent:eventName.GetNSString() withValues:dictionary];
    });
#endif
    GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red, TEXT("logEvent raised"));
}

FString UAppsFlyerSDKBlueprint::getAppsFlyerUID() {
#if PLATFORM_ANDROID
    FString ResultStr = "Undefined";
    if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
    {
        jmethodID MethodId = FJavaWrapper::FindMethod(Env,
                                FJavaWrapper::GameActivityClassID,
                                "afGetAppsFlyerUID",
                                "()Ljava/lang/String;", false);
        ResultStr = FJavaHelper::FStringFromLocalRef(Env, (jstring)FJavaWrapper::CallObjectMethod(Env, FJavaWrapper::GameActivityThis, MethodId));
    }
    return ResultStr;
#elif PLATFORM_IOS
    NSString *UID = [[AppsFlyerLib shared] getAppsFlyerUID];
    FString ueUID(UID);
    return ueUID;
#else
    return FString(TEXT("Wrong platform!"));
#endif
}

FString UAppsFlyerSDKBlueprint::advertisingIdentifier() {
#if PLATFORM_ANDROID
    return FString(TEXT(""));
#elif PLATFORM_IOS
    NSString *IDFA = [[AppsFlyerLib shared] advertisingIdentifier];
    FString ueIDFA(IDFA);
    return ueIDFA;
#else
    return FString(TEXT("Wrong platform!"));
#endif
}

void UAppsFlyerSDKBlueprint::waitForATTUserAuthorizationWithTimeoutInterval(int timeoutInterval) {
#if PLATFORM_IOS
    dispatch_async(dispatch_get_main_queue(), ^ {
        [[AppsFlyerLib shared] waitForATTUserAuthorizationWithTimeoutInterval:timeoutInterval];
    });
#endif
}
