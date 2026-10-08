#pragma once
#include <stddef.h>
namespace wsm {
// Public production command grammar. State/identity checks stay in the owner.
bool valid_command(const char *command);
bool toggle_value(const char *text, bool &enabled);
bool slider_value(const char *text, float minimum, float maximum, float &value);
void escape_json(const char *text, char *out, size_t capacity);
}
