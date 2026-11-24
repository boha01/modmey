# Mod Menu PRO v7 — C++ / NDK / IL2CPP

## ما تم تطويره في الإصدار v7
1. **سلاسة الفتح والغلق والتحريك وتقليل اللاق (`MenuOverlay.java`)**:
   - تسريع الرسوميات بالعتاد (`FLAG_HARDWARE_ACCELERATED` + `LAYER_TYPE_HARDWARE` أثناء الحركات فقط) مع منع تضارب حركات الفتح والغلق عند النقر السريع (`animToken`).
   - سحب وتغيير حجم فوري ومقنّن زمنياً (`8ms–12ms` throttle) بدون إغراق `WindowManager` باستدعاءات متكررة، وإلغاء حركة الالتصاق بالحافة فوراً عند لمس الفقاعة.
   - تقليل الكتابة على القرص: حفظ قيم أشرطة التمرير في `SharedPreferences` يتم عند رفع الإصبع (`onStopTrackingTouch`) وليس مع كل بكسل حركة.
   - خيار **إخفاء القائمة من التسجيل** (`FLAG_SECURE`).
2. **القوة والحماية (`native-lib.cpp`, `Il2CppApi.h`, `KittyMemory.h`)**:
   - **درع أعطال `SEGV-SHIELD`** (`sigaction` + `sigsetjmp`/`siglongjmp`) لحماية عملية الحقن والبحث في IL2CPP من الانهيار عند تغيّر بعض الأسماء أو الأوفستات، مع عدّاد للأعطال المُعالجة.
   - **تخزين مؤقت لنطاق `libil2cpp.so`** (`g_libBase`/`g_libEnd`) وإلغاء قراءة `/proc/self/maps` المتكررة من حلقة الباتش.
   - دعم وضع الأمان عبر ملف `/sdcard/modmenu_safe.txt`.
3. **إضافة جميع الهوكات المستخرجة من `libdatastore_shared.so`**:
   - **SR-SPOOF**: `SystemInfo.get_deviceUniqueIdentifier`، `AuthorizationMetaModel.FromDevice` / `FillSelfExtended`، `SettingsServiceSystem.GetDeviceId`.
   - **ESP & Physics**: `Transform.set_position` / `set_localPosition` / `SetPositionAndRotation`، `PhysicsActor.Update`، `PhysicsActorSync.FixedUpdate`.
   - **Weapon & WallPen**: `WeaponProduceAttackRangedSystem` (`RayCastAttackRanged` 7/8، `OnUpdate`، `ProcessHits` 6، `GetPenetrationParameters`، `TryGetPenetrationDistance`).
   - **Camera / FreeCam / Spectate**: `Camera.get/set_fieldOfView`، `UniversalCameraSystem.ApplyInputWithVerticalRotationLimit`، `CameraFollowingPedSystem` / `CameraFollowingVehicleSystem` / `CameraPlacementSystem`، `SoundFmodPriorityService.SwitchListener`.
   - **SkyWalk / GodMode / Head**: جميع دوال `CharacterActor` و`PedKnockoutParameters` و`FallState` و`PedStateMachine` و`WeaponRangeShootState` و`PedState`.
   - **Health / HUD / FallDamage**: `ElementHealthSyncSystem`، `UpdateHudHealthViewSystem`، `Slider.set_value` / `SetValueWithoutNotify`، `PedViewKnockoutService.ApplyLocalFallDamage`.
   - **Movement / NavGraph / Sync / Range**: `PhysicsActor.get_Velocity` / `FixedUpdate` / `Update`، `PedViewInputsSystem`، `NavigationGraph`، `CLSV-HOOKS`، `PedStaminaSystem`، `SkyLightingDriver`، وحلقة `SR-RANGE` الخماسية.
4. **جسر التوافق للحقن داخل الـ APK (`com/mycompany/application/MainActivity.java`)**:
   - يتيح استدعاء `com.mycompany.application.MainActivity.Start(Context)` من اللعبة (`ChillbaseActivity`) ليقوم أولاً بتشغيل `SilkGuard.install(ctx)` (إن وجدت) ثم إطلاق `com.bonika.modmenu`.

## البناء والتشغيل
> **ملاحظة بيئية:** هذه البيئة لا تحتوي على Android NDK أو Android SDK (`d8`/`android.jar`), لذا يجب بناء المشروع عبر **Android Studio** أو **GitHub Actions** (`.github/workflows/build.yml`).

1. ضع مكتبة **Dobby** الحقيقية (`libdobby.a`) في `app/src/main/cpp/dobby/arm64-v8a/` (استبدالاً للـ stub في GitHub workflow إذا أردت تفعيل الهوكات فعلياً).
2. ابنِ المشروع (`gradle assembleDebug` أو Android Studio > Build APK) لتحصل على:
   - `lib/arm64-v8a/libmodmenu.so`
   - `classes.dex` ← أعد تسميته إلى `classes5.dex` وضعه مكان `classes5.dex` الخاصة بـ `SPINOX` داخل الـ APK.
3. أضف `<service android:name="com.bonika.modmenu.FloatingService" .../>` وصلاحيتي `SYSTEM_ALERT_WINDOW` و`FOREGROUND_SERVICE_SPECIAL_USE` إلى `AndroidManifest.xml` الخاص باللعبة.
