#include "quickjs.h"

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#define ESS_QJS_ABI_EXPORT __declspec(dllexport)
#else
#define ESS_QJS_ABI_EXPORT
#endif

typedef JSValue (*ESSQJSCallback)(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

ESS_QJS_ABI_EXPORT int ess_qjs_sizeof_jsvalue(void)
{
    return (int)sizeof(JSValue);
}

ESS_QJS_ABI_EXPORT int ess_qjs_offset_jsvalue_tag(void)
{
#if defined(JS_NAN_BOXING) && JS_NAN_BOXING
    return -1;
#else
    return (int)offsetof(JSValue, tag);
#endif
}

ESS_QJS_ABI_EXPORT int ess_qjs_sizeof_pointer(void)
{
    return (int)sizeof(void *);
}

ESS_QJS_ABI_EXPORT int ess_qjs_undefined_tag(void)
{
    JSValue v = JS_UNDEFINED;
    return JS_VALUE_GET_TAG(v);
}

ESS_QJS_ABI_EXPORT int ess_qjs_test_pascal_callback(ESSQJSCallback cb, int *out_tag)
{
    JSRuntime *rt;
    JSContext *ctx;
    JSValue ret;
    int tag;

    if (!cb || !out_tag)
        return -1;

    rt = JS_NewRuntime();
    if (!rt)
        return -2;

    ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return -3;
    }

    ret = cb(ctx, JS_UNDEFINED, 0, NULL);
    tag = JS_VALUE_GET_TAG(ret);
    *out_tag = tag;

    JS_FreeValue(ctx, ret);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);

    if (tag == JS_TAG_UNDEFINED)
        return 0;

    return 1;
}
