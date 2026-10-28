#pragma once
#include <stdbool.h>

void arcade_splash(void);
// -1 starts Doom. Zero or more is an index into the GIF store.
int arcade_menu(void);
bool doom_image_present(void);
void launch_doom(void);
