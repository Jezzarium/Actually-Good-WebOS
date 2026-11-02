#include "layout.h"

#include "log.h"

#include <json-c/json.h>

#include <algorithm>
#include <cstdio>

namespace {

std::vector<std::string> strings(json_object *obj, const char *key) {
    std::vector<std::string> out;
    json_object *list;
    if (!json_object_object_get_ex(obj, key, &list) || !json_object_is_type(list, json_type_array)) return out;
    for (size_t i = 0; i < json_object_array_length(list); i++) {
        json_object *v = json_object_array_get_idx(list, i);
        if (json_object_is_type(v, json_type_string)) out.push_back(json_object_get_string(v));
    }
    return out;
}

json_object *array(const std::vector<std::string> &values) {
    json_object *list = json_object_new_array();
    for (const std::string &v : values) json_object_array_add(list, json_object_new_string(v.c_str()));
    return list;
}

}  // namespace

bool Layout::load(const std::string &path) {
    json_object *obj = json_object_from_file(path.c_str());
    if (!obj) return false;
    order = strings(obj, "order");
    hidden = strings(obj, "hidden");
    json_object_put(obj);
    return true;
}

bool Layout::save(const std::string &path) const {
    json_object *obj = json_object_new_object();
    json_object_object_add(obj, "order", array(order));
    json_object_object_add(obj, "hidden", array(hidden));
    // Write beside it and rename, so a crash never leaves half a file.
    std::string tmp = path + ".tmp";
    bool ok = json_object_to_file_ext(tmp.c_str(), obj, JSON_C_TO_STRING_PRETTY) == 0 &&
              std::rename(tmp.c_str(), path.c_str()) == 0;
    json_object_put(obj);
    if (!ok) LOG("layout: can't save %s", path.c_str());
    return ok;
}

bool Layout::is_hidden(const std::string &id) const {
    return std::find(hidden.begin(), hidden.end(), id) != hidden.end();
}

std::vector<App> Layout::apply(const std::vector<App> &apps) const {
    std::vector<App> out;
    for (const std::string &id : order) {
        auto it = std::find_if(apps.begin(), apps.end(), [&](const App &a) { return a.id == id; });
        if (it != apps.end() && !is_hidden(id)) out.push_back(*it);
    }
    for (const App &app : apps) {
        bool placed = std::find(order.begin(), order.end(), app.id) != order.end();
        if (!placed && !is_hidden(app.id)) out.push_back(app);
    }
    return out;
}
