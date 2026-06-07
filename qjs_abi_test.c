#include "quickjs.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef _WIN32
#define ESS_QJS_ABI_EXPORT __declspec(dllexport)
#else
#define ESS_QJS_ABI_EXPORT
#endif

typedef JSValue (*ESSQJSCallback)(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
typedef void (*ESSQJSLogCallback)(const char *text);

enum {
    ESS_QJS_ARG_NULL = 0,
    ESS_QJS_ARG_STRING = 1,
    ESS_QJS_ARG_INT = 2,
    ESS_QJS_ARG_DOUBLE = 3,
    ESS_QJS_ARG_BOOL = 4
};

typedef struct ESSQJSArg {
    int type;
    const char *name;
    const char *string_value;
    int int_value;
    double double_value;
    int bool_value;
} ESSQJSArg;

typedef int (*ESSQJSNativeCallback)(int callback_id, int argc, const ESSQJSArg *argv, ESSQJSArg *result);

static ESSQJSLogCallback ess_qjs_log_callback = NULL;
static ESSQJSNativeCallback ess_qjs_native_callback = NULL;

static void ess_qjs_free_values(JSContext *ctx, int argc, JSValue *values)
{
    int i;

    if (!values)
        return;

    for (i = 0; i < argc; i++)
        JS_FreeValue(ctx, values[i]);

    free(values);
}

static int ess_qjs_make_value(JSContext *ctx, const ESSQJSArg *arg, JSValue *value)
{
    if (!arg || !value)
        return -1;

    switch (arg->type) {
    case ESS_QJS_ARG_NULL:
        *value = JS_NULL;
        return 0;
    case ESS_QJS_ARG_STRING:
        *value = JS_NewString(ctx, arg->string_value ? arg->string_value : "");
        break;
    case ESS_QJS_ARG_INT:
        *value = JS_NewInt32(ctx, arg->int_value);
        return 0;
    case ESS_QJS_ARG_DOUBLE:
        *value = JS_NewFloat64(ctx, arg->double_value);
        return 0;
    case ESS_QJS_ARG_BOOL:
        *value = JS_NewBool(ctx, arg->bool_value != 0);
        return 0;
    default:
        *value = JS_UNDEFINED;
        return 0;
    }

    if (JS_VALUE_GET_TAG(*value) == JS_TAG_EXCEPTION)
        return -2;

    return 0;
}

static int ess_qjs_fill_arg_from_value(JSContext *ctx, JSValueConst value, ESSQJSArg *arg)
{
    int tag;

    if (!arg)
        return -1;

    arg->type = ESS_QJS_ARG_NULL;
    arg->name = NULL;
    arg->string_value = NULL;
    arg->int_value = 0;
    arg->double_value = 0;
    arg->bool_value = 0;

    tag = JS_VALUE_GET_TAG(value);
    switch (tag) {
    case JS_TAG_NULL:
    case JS_TAG_UNDEFINED:
        arg->type = ESS_QJS_ARG_NULL;
        return 0;
    case JS_TAG_BOOL:
        arg->type = ESS_QJS_ARG_BOOL;
        arg->bool_value = JS_VALUE_GET_BOOL(value) != 0;
        return 0;
    case JS_TAG_INT:
        arg->type = ESS_QJS_ARG_INT;
        arg->int_value = JS_VALUE_GET_INT(value);
        return 0;
    case JS_TAG_FLOAT64:
        arg->type = ESS_QJS_ARG_DOUBLE;
        arg->double_value = JS_VALUE_GET_FLOAT64(value);
        return 0;
    default:
        arg->type = ESS_QJS_ARG_STRING;
        arg->string_value = JS_ToCString(ctx, value);
        if (!arg->string_value)
            return -2;
        return 0;
    }
}

static void ess_qjs_free_args(JSContext *ctx, int argc, ESSQJSArg *args)
{
    int i;

    if (!args)
        return;

    for (i = 0; i < argc; i++) {
        if (args[i].type == ESS_QJS_ARG_STRING && args[i].string_value)
            JS_FreeCString(ctx, args[i].string_value);
    }

    free(args);
}

static void ess_qjs_init_arg(ESSQJSArg *arg)
{
    if (!arg)
        return;

    arg->type = ESS_QJS_ARG_NULL;
    arg->name = NULL;
    arg->string_value = NULL;
    arg->int_value = 0;
    arg->double_value = 0;
    arg->bool_value = 0;
}

static JSValue ess_qjs_native_bridge(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
    ESSQJSArg *args = NULL;
    ESSQJSArg result_arg;
    JSValue result_value;
    int callback_result;
    int i;

    (void)this_val;
    ess_qjs_init_arg(&result_arg);

    if (argc > 0) {
        args = (ESSQJSArg *)calloc((size_t)argc, sizeof(ESSQJSArg));
        if (!args)
            return JS_EXCEPTION;

        for (i = 0; i < argc; i++) {
            if (ess_qjs_fill_arg_from_value(ctx, argv[i], &args[i]) != 0) {
                ess_qjs_free_args(ctx, argc, args);
                return JS_EXCEPTION;
            }
        }
    }

    if (!ess_qjs_native_callback) {
        ess_qjs_free_args(ctx, argc, args);
        return JS_UNDEFINED;
    }

    callback_result = ess_qjs_native_callback(magic, argc, args, &result_arg);
    if (callback_result != 0) {
        ess_qjs_free_args(ctx, argc, args);
        return JS_UNDEFINED;
    }

    if (ess_qjs_make_value(ctx, &result_arg, &result_value) != 0) {
        ess_qjs_free_args(ctx, argc, args);
        return JS_EXCEPTION;
    }

    ess_qjs_free_args(ctx, argc, args);
    return result_value;
}

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

ESS_QJS_ABI_EXPORT void ess_qjs_set_native_callback(ESSQJSNativeCallback cb)
{
    ess_qjs_native_callback = cb;
}

ESS_QJS_ABI_EXPORT int ess_qjs_register_native_function(JSContext *ctx, const char *name, int callback_id, int length)
{
    JSValue global;
    JSValue func;
    JSCFunctionType ft;
    int ret;

    if (!ctx || !name)
        return -1;

    ft.generic_magic = ess_qjs_native_bridge;

    global = JS_GetGlobalObject(ctx);
    func = JS_NewCFunction2(ctx, ft.generic, name, length, JS_CFUNC_generic_magic, callback_id);
    if (JS_VALUE_GET_TAG(func) == JS_TAG_EXCEPTION) {
        JS_FreeValue(ctx, global);
        return -2;
    }

    ret = JS_SetPropertyStr(ctx, global, name, func);
    JS_FreeValue(ctx, global);

    if (ret < 0)
        return -3;

    return 0;
}

ESS_QJS_ABI_EXPORT int ess_qjs_call_function(JSContext *ctx, const char *name, int argc, const ESSQJSArg *argv)
{
    JSValue global;
    JSValue func;
    JSValue result;
    JSValue *js_args = NULL;
    int i;

    if (!ctx || !name)
        return -1;

    if (argc < 0)
        return -2;

    if (argc > 0 && !argv)
        return -3;

    global = JS_GetGlobalObject(ctx);
    func = JS_GetPropertyStr(ctx, global, name);
    JS_FreeValue(ctx, global);

    if (JS_VALUE_GET_TAG(func) == JS_TAG_EXCEPTION)
        return -4;

    if (!JS_IsFunction(ctx, func)) {
        JS_FreeValue(ctx, func);
        return -5;
    }

    if (argc > 0) {
        js_args = (JSValue *)calloc((size_t)argc, sizeof(JSValue));
        if (!js_args) {
            JS_FreeValue(ctx, func);
            return -6;
        }

        for (i = 0; i < argc; i++) {
            if (ess_qjs_make_value(ctx, &argv[i], &js_args[i]) != 0) {
                ess_qjs_free_values(ctx, i, js_args);
                JS_FreeValue(ctx, func);
                return -7;
            }
        }
    }

    result = JS_Call(ctx, func, JS_UNDEFINED, argc, js_args);
    ess_qjs_free_values(ctx, argc, js_args);
    JS_FreeValue(ctx, func);

    if (JS_VALUE_GET_TAG(result) == JS_TAG_EXCEPTION)
        return -8;

    JS_FreeValue(ctx, result);
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
