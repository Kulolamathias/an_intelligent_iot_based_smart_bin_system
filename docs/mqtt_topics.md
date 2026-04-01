1. Device‑specific topics (web dashboard, remote commands)
Base: devices/<mac>/ – where <mac> is the bin’s MAC address (lowercase, no separators).

Topic	Direction	Purpose
devices/<mac>/status/online	Publish	Bin announces itself online (retained)
devices/<mac>/data	Publish	Regular sensor data (fill level, battery, etc.)
devices/<mac>/cmd	Subscribe	Receive commands from web dashboard (e.g., open lid, reboot)
devices/<mac>/cmd/registration	Subscribe	Registration command from dashboard
devices/<mac>/response	Publish	Acknowledge or reply to commands
Build using mqtt_topic_build():


char topic[128];
mqtt_topic_build(topic, sizeof(topic), "status/online");
mqtt_topic_build(topic, sizeof(topic), "data");
mqtt_topic_build(topic, sizeof(topic), "cmd");
mqtt_topic_build(topic, sizeof(topic), "cmd/registration");
mqtt_topic_build(topic, sizeof(topic), "response");
2. Inter‑bin communication topics (peer discovery and status)
Base: smartbin/ (no MAC, shared among all bins).

Topic	Direction	Purpose
smartbin/discovery/announce	Publish	Bin announces its presence (with ID, location, fill)
smartbin/discovery/query	Subscribe	Request all bins to announce
smartbin/bin/<id>/state	Publish	Current fill level and status of a specific bin
smartbin/bin/<id>/redirect	Publish	Redirect user to another bin (full bin)
Here <id> is the bin’s MAC address (same as used in device topics) or a short ID.

These topics do not use the device‑specific base. ..need separate helper functions or a second topic builder. For simplicity, define constants and a helper:


// In mqtt_topic.h
esp_err_t mqtt_topic_build_interbin(char *out, size_t size, const char *subpath, const char *bin_id);
Implementation example:


esp_err_t mqtt_topic_build_interbin(char *out, size_t size, const char *subpath, const char *bin_id) {
    return snprintf(out, size, "smartbin/%s/%s", subpath, bin_id) < (int)size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}



Base topic: devices/ece3348df940/
Subscribing to devices/ece3348df940/cmd/testmqtt
Publishing devices/ece3348df940/status/online