#include "luna.h"

#include "log.h"

#ifdef HOME_WEBOS
#include <glib.h>
#include <luna-service2/lunaservice.h>
#endif

namespace {

#ifdef HOME_WEBOS
struct Pending {
    Luna::Reply reply;
    bool once;
};

bool on_message(LSHandle *, LSMessage *msg, void *ctx) {
    auto *pending = static_cast<Pending *>(ctx);
    if (pending->reply) {
        json_object *obj = json_tokener_parse(LSMessageGetPayload(msg));
        if (obj) {
            pending->reply(obj);
            json_object_put(obj);
        }
    }
    if (pending->once) delete pending;
    return true;
}

void report(const char *what, LSError &err) {
    LOG("luna: %s: %s", what, err.message);
    LSErrorFree(&err);
}
#endif

}  // namespace

bool Luna::connect(const char *app_id) {
#ifdef HOME_WEBOS
    LSError err;
    LSErrorInit(&err);
    LSHandle *sh = nullptr;
    // LG's SDL registers the app's own name in this process, so fall back to an
    // anonymous connection, which the app's role also allows.
    if (!LSRegister(app_id, &sh, &err)) {
        report(app_id, err);
        LSErrorInit(&err);
        if (!LSRegister(nullptr, &sh, &err)) {
            report("register", err);
            return false;
        }
    }
    if (!LSGmainContextAttach(sh, g_main_context_default(), &err)) {
        report("attach", err);
        return false;
    }
    handle_ = sh;
    return true;
#else
    (void)app_id;
    return false;
#endif
}

void Luna::call(const std::string &uri, const std::string &payload, Reply on_reply) {
#ifdef HOME_WEBOS
    if (!handle_) return;
    LSError err;
    LSErrorInit(&err);
    auto *pending = new Pending{std::move(on_reply), true};
    if (!LSCallOneReply(static_cast<LSHandle *>(handle_), uri.c_str(), payload.c_str(), on_message, pending,
                        nullptr, &err)) {
        report(uri.c_str(), err);
        delete pending;
    }
#else
    (void)uri, (void)payload, (void)on_reply;
#endif
}

void Luna::subscribe(const std::string &uri, const std::string &payload, Reply on_reply) {
#ifdef HOME_WEBOS
    if (!handle_) return;
    LSError err;
    LSErrorInit(&err);
    auto *pending = new Pending{std::move(on_reply), false};
    if (!LSCall(static_cast<LSHandle *>(handle_), uri.c_str(), payload.c_str(), on_message, pending, nullptr,
                &err)) {
        report(uri.c_str(), err);
        delete pending;
    }
#else
    (void)uri, (void)payload, (void)on_reply;
#endif
}

void Luna::pump() {
#ifdef HOME_WEBOS
    while (g_main_context_iteration(nullptr, FALSE)) {
    }
#endif
}

std::string get_string(json_object *obj, const char *key, const std::string &fallback) {
    json_object *v;
    if (!obj || !json_object_object_get_ex(obj, key, &v) || !json_object_is_type(v, json_type_string)) {
        return fallback;
    }
    return json_object_get_string(v);
}

bool get_bool(json_object *obj, const char *key, bool fallback) {
    json_object *v;
    if (!obj || !json_object_object_get_ex(obj, key, &v)) return fallback;
    return json_object_get_boolean(v);
}
