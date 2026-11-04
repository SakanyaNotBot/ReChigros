// The iOS backend includes AVFoundation and Objective-C runtime types, so the miniaudio
// implementation has to be compiled by the Objective-C front end. Keeping the shared
// implementation in the .c file lets the desktop trees keep compiling it as C.
#include "MiniaudioImpl.c"
