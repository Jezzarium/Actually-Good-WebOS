#pragma once

// The TV's external inputs (HDMI), from com.webos.service.eim/getAllInputStatus.

#include <json-c/json.h>

#include <string>
#include <vector>

struct Input {
    std::string label;   // the device's name over CEC if it has one, else "HDMI 2"
    std::string port;    // "HDMI 2"
    std::string app_id;  // launching it switches to the input
    bool connected;
};

std::vector<Input> inputs_from_status(json_object *reply);
