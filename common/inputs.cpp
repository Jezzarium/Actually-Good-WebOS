#include "inputs.h"

#include "luna.h"

std::vector<Input> inputs_from_status(json_object *reply) {
    std::vector<Input> inputs;
    json_object *devices;
    if (!json_object_object_get_ex(reply, "devices", &devices) || !json_object_is_type(devices, json_type_array)) {
        return inputs;
    }
    for (size_t i = 0; i < json_object_array_length(devices); i++) {
        json_object *dev = json_object_array_get_idx(devices, i);
        Input input;
        input.app_id = get_string(dev, "appId");
        if (input.app_id.empty()) continue;
        // "HDMI_2" -> "HDMI 2"
        input.port = get_string(dev, "id");
        for (char &c : input.port) {
            if (c == '_') c = ' ';
        }
        input.label = get_string(dev, "label", input.port);
        input.connected = get_bool(dev, "connected");
        inputs.push_back(std::move(input));
    }
    return inputs;
}
