#include "quickjs.h"

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#define ESS_QJS_ABI_EXPORT __declspec(dllexport)
#else
#define ESS_QJS_ABI_EXPORT
#endif

typedef JSValue (*ESSQJSCallback)(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
typedef void (*ESSQJSLogCallback)(const char *text);

static ESSQJSLogCallback ess_qjs_log_callback = NULL;

static JSValue ess_qjs_log_bridge(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    const char *text = NULL;

    (void)this_val;

    if (argc > 0)
        text = JS_ToCString(ctx, argv[0]);

    if (ess_qjs_log_callback)
        ess_qjs_log_callback(text ? text : "");

    if (text)
        JS_FreeCString(ctx, text);

    return JS_UNDEFINED;
}

ESS_QJS_ABI_EXPORT void ess_qjs_set_log_callback(ESSQJSLogCallback cb)
{
    ess_qjs_log_callback = cb;
}

ESS_QJS_ABI_EXPORT int ess_qjs_register_log_function(JSContext *ctx, const char *name)
{
    JSValue global;
    JSValue func;
    const char *function_name;
    int ret;

    if (!ctx)
        return -1;

    function_name = name ? name : "essLog";
    global = JS_GetGlobalObject(ctx);
    func = JS_NewCFunction2(ctx, ess_qjs_log_bridge, function_name, 1, JS_CFUNC_generic, 0);
    if (JS_VALUE_GET_TAG(func) == JS_TAG_EXCEPTION) {
        JS_FreeValue(ctx, global);
        return -2;
    }

    ret = JS_SetPropertyStr(ctx, global, function_name, func);
    JS_FreeValue(ctx, global);

    if (ret < 0)
        return -3;

    return 0;
}

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
