#pragma once

// Calls to webOS services over the Luna bus (libluna-service2), answered on
// the GLib main context, which pump() runs without blocking once per frame.
// Desktop builds have no bus: connect() returns false and calls do nothing.

#include <json-c/json.h>

#include <functional>
#include <string>

class Luna {
public:
    // Called with each reply's parsed payload; the object is freed afterwards.
    using Reply = std::function<void(json_object *reply)>;

    bool connect(const char *app_id);
    // One reply.
    void call(const std::string &uri, const std::string &payload, Reply on_reply = nullptr);
    // A reply now and one per change, for the life of the app.
    void subscribe(const std::string &uri, const std::string &payload, Reply on_reply);
    void pump();

private:
    void *handle_ = nullptr;  // LSHandle
};

// Small helpers for reading json-c objects.
std::string get_string(json_object *obj, const char *key, const std::string &fallback = "");
bool get_bool(json_object *obj, const char *key, bool fallback = false);
