#pragma once
// PLACEHOLDER HEADER - استبدله بالهيدر الحقيقي من Dobby (github.com/jmpews/Dobby)
// وضع libdobby.a / libdobby.so في app/src/main/cpp/dobby/ قبل البناء النهائي.
// هذا التعريف يكفي لربط الكود؛ CMake سيربط المكتبة الحقيقية إن وُجدت.
#ifdef __cplusplus
extern "C" {
#endif
int DobbyHook(void *address, void *replace_call, void **origin_call);
int DobbyDestroy(void *address);
#ifdef __cplusplus
}
#endif
